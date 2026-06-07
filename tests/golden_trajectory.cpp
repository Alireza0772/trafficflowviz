// Golden-trajectory test for the agent simulation (Phases 2-3).
//
// Standalone, headless (no SDL/Engine/rendering), hermetic (network + vehicles +
// signs built programmatically; Configuration is NOT initialized so getter
// defaults apply and no stray .conf can perturb the run).
//
// Gates:
//   1) route() correctness   : deterministic BFS returns the hand-computed paths
//                              (digest-independent; guards the Phase-3 routing fix).
//   2) run-twice determinism : two in-process runs produce the same digest.
//   3) golden digest         : digest matches kGolden (regenerate with --print-hash).
//   4) IDM + sign invariants : no NaN/Inf; speeds bounded; a free vehicle accelerates;
//                              a vehicle actually stops at the STOP sign.

#include "agents/Action.hpp"
#include "agents/IdmParams.hpp"
#include "agents/NNBrain.hpp"
#include "agents/VtableBrain.hpp"
#include "agents/tfv_brain.h"
#include "core/Configuration.hpp"
#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "core/TrafficEntity.hpp"
#include "io/TrajectoryExporter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <glm/glm.hpp>
#include <memory>
#include <sstream>
#include <vector>

using namespace tfv;

// Committed reference digest (0 = not yet pinned; run with --print-hash to obtain).
static constexpr uint64_t kGolden = 0x9cb6732614cb624cULL; // hashes laneIndex (Phase 6)
static constexpr uint64_t kGoldenMultiLane = 0x9a695a4adfbfd3e7ULL; // Gate 9 overtake
static constexpr uint64_t kGoldenNN = 0xf164396f8160724dULL;        // Gate 10 NN determinism
static constexpr uint64_t kGoldenNNPop = 0x8a1cc6b5bd837f1aULL;     // Gate 15 NN bank (population=4)
static constexpr uint64_t kGoldenTwoWay = 0xf92e9fb995110cc1ULL;   // Gate 16 two-way road
static constexpr uint64_t kGoldenVtable = 0x626e52c4a6c7691bULL;   // Gate 11 vtable adapter
static constexpr uint64_t kGoldenTrajectoryCsv = 0x6498c43e5c3ee49cULL; // Gate 12 export CSV
static constexpr uint64_t kGoldenCrossLane = 0xf61e1bbd64720560ULL; // Gate 14 lane-aware cross-seg

namespace
{
    // Node ids start at 1 so that destNode==0 means "wander / no destination".
    // Topology: a triangular loop 1->2->3->1 with a fork at node 2 (-> node 4 -> 1).
    //   seg0: 1->2   seg1: 2->3   seg2: 3->1   seg3: 2->4 (fork)   seg4: 4->1
    // A STOP sign sits on seg0 at pos 0.85.
    RoadNetwork makeScenarioNetwork()
    {
        RoadNetwork net;

        auto addNode = [&](uint32_t id, float x, float y) {
            Node n;
            n.id = id;
            n.pos = {x, y};
            net.addNode(n); // outgoing is populated solely by addSegment (no pre-set)
        };
        addNode(1, 0, 0);
        addNode(2, 100, 0);
        addNode(3, 100, 100);
        addNode(4, 0, 100);

        auto addSeg = [&](uint32_t id, uint32_t from, uint32_t to, glm::vec2 dir, float len) {
            RoadSegment s;
            s.id = id;
            s.fromNode = from;
            s.toNode = to;
            s.dir = dir;
            s.length = len;
            s.speedLimit = 13.9f;
            net.addSegment(s);
        };
        addSeg(0, 1, 2, {1, 0}, 100.0f);
        addSeg(1, 2, 3, {0, 1}, 100.0f);
        addSeg(2, 3, 1, glm::normalize(glm::vec2(-1, -1)), 141.4f);
        addSeg(3, 2, 4, glm::normalize(glm::vec2(-1, 1)), 141.4f); // fork at node 2
        addSeg(4, 4, 1, {0, -1}, 100.0f);

        Sign stop;
        stop.id = 1;
        stop.type = SignType::STOP;
        stop.segmentId = 0;
        stop.pos = 0.85f;
        net.addSign(stop);

        return net;
    }

    std::vector<Vehicle> makeScenarioVehicles()
    {
        std::vector<Vehicle> v(4);
        // Leader on seg0 heading for the fork (destNode 4 -> routes via seg3 at node 2).
        v[0].id = 1;
        v[0].segmentId = 0;
        v[0].position = 0.40f;
        v[0].vel = {12.0f, 0.0f};
        v[0].destNode = 4;
        // Follower on seg0 (wander).
        v[1].id = 2;
        v[1].segmentId = 0;
        v[1].position = 0.15f;
        v[1].vel = {12.0f, 0.0f};
        // Free vehicle starting at rest on seg1 (no sign) -> must accelerate.
        v[2].id = 3;
        v[2].segmentId = 1;
        v[2].position = 0.30f;
        v[2].vel = {0.0f, 0.0f};
        // Vehicle on seg4 (4->1) approaching the SIGNALIZED node 1: must stop at red,
        // then proceed on green. It is alone on seg4, so a stop there is the light.
        v[3].id = 4;
        v[3].segmentId = 4;
        v[3].position = 0.90f; // close to node 1 so it meets the first (red) phase
        v[3].vel = {4.0f, 0.0f};
        return v;
    }

    struct RunResult
    {
        uint64_t digest{0};
        float maxSpeed{0.0f};
        float freeVehicleSpeed{0.0f};
        bool anyNonFinite{false};
        bool sawStop{false};         // a vehicle reached ~0 speed near the STOP sign on seg0
        bool leaderProceeded{false}; // the stopping leader (id 1) later left seg0 (no deadlock)
        bool sawLightStop{false};    // veh 4 stopped at the red light at node 1 (end of seg4)
        bool lightProceeded{false};  // veh 4 later cleared node 1 onto seg0 (green released it)
        bool anyLaneChange{false};   // any vehicle left lane 0 (must NOT happen: single-lane net)
    };

    RunResult runOnce()
    {
        RoadNetwork net = makeScenarioNetwork();
        Simulation sim(&net);
        sim.initialize(makeScenarioVehicles());

        const double dt = 0.02; // 50 Hz
        RunResult r;
        VehicleMap snap;
        for(int t = 0; t < 600; ++t)
        {
            sim.update(dt);
            snap = sim.snapshot();
            for(const auto& [id, v] : snap)
            {
                if(v.segmentId == 0 && v.position > 0.75f && v.position < 0.92f &&
                   glm::length(v.vel) < 0.5f)
                    r.sawStop = true;
                if(id == 1 && v.segmentId == 0 && v.position > 0.86f)
                    r.leaderProceeded = true; // cleared the STOP (0.85) and moved past it
                if(id == 4 && v.segmentId == 4 && v.position > 0.9f && glm::length(v.vel) < 0.5f)
                    r.sawLightStop = true; // halted at the red light at node 1
                if(id == 4 && v.segmentId == 0)
                    r.lightProceeded = true; // released on green, crossed node 1 onto seg0
                if(v.laneIndex != 0)
                    r.anyLaneChange = true; // single-lane network -> MOBIL must be a no-op
            }
        }

        std::vector<uint64_t> ids;
        ids.reserve(snap.size());
        for(const auto& [id, v] : snap)
            ids.push_back(id);
        std::sort(ids.begin(), ids.end());

        uint64_t h = 1469598103934665603ULL;
        auto mix = [&](uint64_t x) {
            h ^= x;
            h *= 1099511628211ULL;
        };
        for(uint64_t id : ids)
        {
            const Vehicle& v = snap.at(id);
            const float speed = glm::length(v.vel);
            if(!std::isfinite(v.position) || !std::isfinite(speed))
                r.anyNonFinite = true;
            mix(id);
            mix(v.segmentId);
            mix(static_cast<uint64_t>(v.laneIndex)); // lane state is digest-protected (Phase 6)
            mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(v.position * 1e6))));
            mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(speed * 1e3))));
            r.maxSpeed = std::max(r.maxSpeed, speed);
            if(id == 3)
                r.freeVehicleSpeed = speed;
        }
        r.digest = h;
        return r;
    }
} // namespace

// Gate 8: cross-segment leader. A follower near the end of seg0 with a (near-)
// stationary leader just past the node on its path (seg1) must brake; with NO such
// leader it stays fast. Distinguishes cross-segment lookahead from same-segment-only.
static bool crossSegmentGate()
{
    auto runFollower = [](bool withLeader) -> float {
        RoadNetwork net;
        auto addNode = [&](uint32_t id, float x, float y) {
            Node n;
            n.id = id;
            n.pos = {x, y};
            net.addNode(n);
        };
        addNode(1, 0, 0);
        addNode(2, 100, 0);
        addNode(3, 200, 0);
        auto addSeg = [&](uint32_t id, uint32_t f, uint32_t t) {
            RoadSegment s;
            s.id = id;
            s.fromNode = f;
            s.toNode = t;
            s.dir = {1, 0};
            s.length = 100.0f;
            s.speedLimit = 13.9f;
            net.addSegment(s);
        };
        addSeg(0, 1, 2);
        addSeg(1, 2, 3);

        std::vector<Vehicle> v;
        Vehicle follower;
        follower.id = 1;
        follower.segmentId = 0;
        follower.position = 0.90f;
        follower.vel = {12.0f, 0.0f};
        v.push_back(follower);
        if(withLeader)
        {
            Vehicle lead;
            lead.id = 2;
            lead.segmentId = 1; // just past the node on the follower's path
            lead.position = 0.05f;
            lead.vel = {0.0f, 0.0f};
            v.push_back(lead);
        }
        Simulation sim(&net);
        sim.initialize(std::move(v));
        for(int t = 0; t < 30; ++t)
            sim.update(0.02);
        return glm::length(sim.snapshot().at(1).vel);
    };

    const float withLead = runFollower(true);
    const float noLead = runFollower(false);
    std::printf("cross-seg gate: follower speed with leader=%.3f, free=%.3f\n", withLead, noLead);
    return withLead < noLead - 2.0f; // braked for the car just past the node
}

// Gate 9: MOBIL overtake on a 2-lane segment. A fast follower behind a slow truck
// (lane 0) must change to lane 1, pass it, and end ahead — deterministically, with a
// turn signal and without ever overlapping another car in the same lane.
struct MultiLaneResult
{
    uint64_t digest{0};
    bool sawLaneChange{false};
    bool overtook{false};
    bool sawSignal{false};
    bool overlap{false};
};
static MultiLaneResult multiLaneRun()
{
    RoadNetwork net;
    Node n1, n2;
    n1.id = 1;
    n1.pos = {0, 0};
    n2.id = 2;
    n2.pos = {2000, 0};
    net.addNode(n1);
    net.addNode(n2);
    RoadSegment s;
    s.id = 0;
    s.fromNode = 1;
    s.toNode = 2;
    s.dir = {1, 0};
    s.length = 2000.0f;
    s.speedLimit = 13.9f;
    s.lanes = 2;
    net.addSegment(s);

    std::vector<Vehicle> v(2);
    v[0].id = 1; // slow truck in lane 0
    v[0].segmentId = 0;
    v[0].position = 0.105f;
    v[0].laneIndex = 0;
    v[0].vel = {3.0f, 0.0f};
    v[0].maxSpeed = 3.0f;
    v[1].id = 2; // fast follower in lane 0, just behind
    v[1].segmentId = 0;
    v[1].position = 0.100f;
    v[1].laneIndex = 0;
    v[1].vel = {10.0f, 0.0f};

    Simulation sim(&net);
    sim.initialize(std::move(v));

    MultiLaneResult r;
    VehicleMap snap;
    const double dt = 0.02;
    for(int t = 0; t < 600; ++t)
    {
        sim.update(dt);
        snap = sim.snapshot();
        const Vehicle& f = snap.at(2);
        if(f.laneIndex == 1)
            r.sawLaneChange = true;
        if(f.lightBits & (light::LEFT | light::RIGHT))
            r.sawSignal = true;
        const Vehicle& a = snap.at(1);
        if(a.segmentId == f.segmentId && a.laneIndex == f.laneIndex)
        {
            const float gap =
                std::fabs(a.position - f.position) * 2000.0f - 0.5f * (a.length + f.length);
            if(gap < 2.0f) // < s0: an overlap / two-into-one-gap
                r.overlap = true;
        }
    }
    const Vehicle& leadEnd = snap.at(1);
    const Vehicle& folEnd = snap.at(2);
    r.overtook = (folEnd.segmentId == leadEnd.segmentId && folEnd.position > leadEnd.position);

    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](uint64_t x) {
        h ^= x;
        h *= 1099511628211ULL;
    };
    for(uint64_t id : {uint64_t{1}, uint64_t{2}})
    {
        const Vehicle& vv = snap.at(id);
        const float sp = glm::length(vv.vel);
        mix(id);
        mix(vv.segmentId);
        mix(static_cast<uint64_t>(vv.laneIndex));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(vv.position * 1e6))));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(sp * 1e3))));
    }
    r.digest = h;
    return r;
}

// Gate 10: the built-in neural-net brain. A random/untrained net drives erratically
// but must stay (a) deterministic same-build (run-twice identical), (b) bounded +
// finite (every accel passes the validator clamp), and (c) collision-free in a lane
// (every lane change passes the Phase-B0 gap re-check). The net is injected directly
// with a fixed architecture + seed, independent of Configuration, so the rule gates
// are untouched.
struct NNResult
{
    uint64_t digest{0};
    bool finite{true};
    bool bounded{true};
    bool overlap{false};
    float maxSpeed{0.0f};
};
static NNResult nnRun(int population = 1)
{
    RoadNetwork net;
    Node n1, n2;
    n1.id = 1;
    n1.pos = {0, 0};
    n2.id = 2;
    n2.pos = {2000, 0};
    net.addNode(n1);
    net.addNode(n2);
    RoadSegment s;
    s.id = 0;
    s.fromNode = 1;
    s.toNode = 2;
    s.dir = {1, 0};
    s.length = 2000.0f;
    s.speedLimit = 13.9f;
    s.lanes = 2;
    net.addSegment(s);

    std::vector<Vehicle> v(4);
    for(int i = 0; i < 4; ++i)
    {
        v[i].id = static_cast<uint64_t>(i + 1);
        v[i].segmentId = 0;
        v[i].position = 0.10f + 0.06f * i;
        v[i].laneIndex = static_cast<uint8_t>(i % 2);
        v[i].vel = {6.0f, 0.0f};
    }
    Simulation sim(&net);
    sim.initialize(std::move(v));
    IdmParams idm;
    sim.setBrainForTest(
        std::make_unique<NNBrain>(std::vector<int>{24, 8, 4}, "tanh", 12345ULL, idm, 0.5f,
                                  population));

    NNResult r;
    VehicleMap snap;
    const double dt = 0.02;
    for(int t = 0; t < 600; ++t)
    {
        sim.update(dt);
        snap = sim.snapshot();
        std::vector<const Vehicle*> vs;
        for(const auto& [id, vv] : snap)
        {
            (void)id;
            const float sp = glm::length(vv.vel);
            if(!std::isfinite(sp) || !std::isfinite(vv.position))
                r.finite = false;
            if(sp < -0.01f || sp > idm.v0_cap + 1.0f)
                r.bounded = false;
            r.maxSpeed = std::max(r.maxSpeed, sp);
            vs.push_back(&vv);
        }
        for(std::size_t a = 0; a < vs.size(); ++a)
            for(std::size_t b = a + 1; b < vs.size(); ++b)
                if(vs[a]->segmentId == vs[b]->segmentId && vs[a]->laneIndex == vs[b]->laneIndex)
                {
                    const float gap = std::fabs(vs[a]->position - vs[b]->position) * 2000.0f -
                                      0.5f * (vs[a]->length + vs[b]->length);
                    if(gap < 2.0f)
                        r.overlap = true;
                }
    }

    std::vector<uint64_t> ids;
    for(const auto& [id, vv] : snap)
    {
        (void)vv;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](uint64_t x) {
        h ^= x;
        h *= 1099511628211ULL;
    };
    for(uint64_t id : ids)
    {
        const Vehicle& vv = snap.at(id);
        const float sp = glm::length(vv.vel);
        mix(id);
        mix(vv.segmentId);
        mix(static_cast<uint64_t>(vv.laneIndex));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(vv.position * 1e6))));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(sp * 1e3))));
    }
    r.digest = h;
    return r;
}

// Gate 11: the C-ABI vtable adapter (VtableBrain). A static in-process "cruise" brain
// implementing tfv_brain.h is wrapped via VtableBrain and run — exercising the adapter
// + ABI gates without needing a real .so. Asserts run-twice determinism + bounded.
namespace
{
    void vt_destroy(void*) {}
    void vt_decide(void*, const float* obs, int n, int obsLen, tfv_action* out)
    {
        for(int k = 0; k < n; ++k)
        {
            const float* o = obs + static_cast<std::size_t>(k) * obsLen;
            const float speed = o[0] * 40.0f, v0 = o[4] * 40.0f;
            float a = (v0 - speed) * 0.4f;
            if(a > 1.5f)
                a = 1.5f;
            if(a < -6.0f)
                a = -6.0f;
            tfv_action act{};
            act.accel = a;
            act.lightCmd = (a < -0.5f) ? 1u : 0u;
            out[k] = act;
        }
    }
    void vt_reset(void*, uint64_t) {}
    const char* vt_hash(void*) { return "static-cruise"; }
    int vt_dlc(void*) { return 0; }
    const tfv_brain_vtable kVT = {sizeof(tfv_brain_vtable), vt_destroy, vt_decide,
                                  vt_reset,                 vt_hash,    vt_dlc};
} // namespace

struct VtResult
{
    uint64_t digest{0};
    bool finite{true};
    bool bounded{true};
    bool abiOk{false};
};
static VtResult vtableRun()
{
    VtResult r;
    tfv_brain_desc desc{};
    desc.abiVersion = TFV_BRAIN_ABI_VERSION;
    desc.obsLen = TFV_BRAIN_OBS_LEN;
    desc.actLayout = TFV_BRAIN_ACT_LAYOUT;
    desc.structSize = static_cast<uint32_t>(sizeof(tfv_brain_desc));
    desc.kindName = "static-cruise";
    r.abiOk = tfv_brain_abi_ok(&desc) && tfv_brain_vtable_ok(&kVT);

    RoadNetwork net;
    Node n1, n2;
    n1.id = 1;
    n1.pos = {0, 0};
    n2.id = 2;
    n2.pos = {1000, 0};
    net.addNode(n1);
    net.addNode(n2);
    RoadSegment s;
    s.id = 0;
    s.fromNode = 1;
    s.toNode = 2;
    s.dir = {1, 0};
    s.length = 1000.0f;
    s.speedLimit = 13.9f;
    s.lanes = 1;
    net.addSegment(s);
    std::vector<Vehicle> v(3);
    for(int i = 0; i < 3; ++i)
    {
        v[i].id = static_cast<uint64_t>(i + 1);
        v[i].segmentId = 0;
        v[i].position = 0.2f + 0.2f * i;
        v[i].vel = {4.0f, 0.0f};
    }
    Simulation sim(&net);
    sim.initialize(std::move(v));
    sim.setBrainForTest(std::make_unique<VtableBrain>(desc, &kVT, nullptr, false));

    VehicleMap snap;
    const double dt = 0.02;
    IdmParams idm;
    for(int t = 0; t < 400; ++t)
    {
        sim.update(dt);
        snap = sim.snapshot();
        for(const auto& [id, vv] : snap)
        {
            (void)id;
            const float sp = glm::length(vv.vel);
            if(!std::isfinite(sp) || !std::isfinite(vv.position))
                r.finite = false;
            if(sp < -0.01f || sp > idm.v0_cap + 1.0f)
                r.bounded = false;
        }
    }
    std::vector<uint64_t> ids;
    for(const auto& [id, vv] : snap)
    {
        (void)vv;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](uint64_t x) {
        h ^= x;
        h *= 1099511628211ULL;
    };
    for(uint64_t id : ids)
    {
        const Vehicle& vv = snap.at(id);
        mix(id);
        mix(vv.segmentId);
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(vv.position * 1e6))));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(glm::length(vv.vel) * 1e3))));
    }
    r.digest = h;
    return r;
}

// Gate 12: trajectory export. Re-run the golden scenario with a TrajectoryExporter into a
// string; the export must (a) leave the sim's state digest byte-identical to the golden
// (proving it is side-effect-free) and (b) produce a deterministic, format-pinned CSV.
struct CsvResult
{
    uint64_t csvDigest{0};
    uint64_t stateDigest{0};
};
static CsvResult trajectoryCsvRun()
{
    RoadNetwork net = makeScenarioNetwork();
    Simulation sim(&net);
    sim.initialize(makeScenarioVehicles());
    std::ostringstream oss;
    TrajectoryExporter exporter(oss, 1);
    const double dt = 0.02;
    VehicleMap snap;
    for(int t = 0; t < 600; ++t)
    {
        sim.update(dt);
        exporter.sample(static_cast<uint64_t>(t), t * dt, sim); // AFTER update (no deadlock)
        snap = sim.snapshot();
    }
    CsvResult r;
    const std::string s = oss.str();
    uint64_t h = 1469598103934665603ULL;
    for(unsigned char c : s)
    {
        h ^= c;
        h *= 1099511628211ULL;
    }
    r.csvDigest = h;

    std::vector<uint64_t> ids;
    for(const auto& [id, v] : snap)
    {
        (void)v;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    uint64_t sh = 1469598103934665603ULL;
    auto mix = [&](uint64_t x) {
        sh ^= x;
        sh *= 1099511628211ULL;
    };
    for(uint64_t id : ids)
    {
        const Vehicle& v = snap.at(id);
        const float sp = glm::length(v.vel);
        mix(id);
        mix(v.segmentId);
        mix(static_cast<uint64_t>(v.laneIndex));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(v.position * 1e6))));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(sp * 1e3))));
    }
    r.stateDigest = sh;
    return r;
}

// Gate 13: Simulation::inspect() (observability engine) must agree with reality AND
// faithfully reproduce the leader + sector perception (not just self state). Scenario:
// a 2-lane segment with a follower (id2, lane 0) behind a leader (id1, lane 0) and a
// side car (id3, lane 1) right beside the follower — which blocks any lane change, so
// the arrangement is stable and exercises the lane-aware leader + a side sector.
static bool inspectGate()
{
    RoadNetwork net;
    Node n1, n2;
    n1.id = 1;
    n1.pos = {0, 0};
    n2.id = 2;
    n2.pos = {1000, 0};
    net.addNode(n1);
    net.addNode(n2);
    RoadSegment s;
    s.id = 0;
    s.fromNode = 1;
    s.toNode = 2;
    s.dir = {1, 0};
    s.length = 1000.0f;
    s.speedLimit = 13.9f;
    s.lanes = 2;
    net.addSegment(s);
    std::vector<Vehicle> v(3);
    v[0].id = 1; // leader, lane 0
    v[0].segmentId = 0;
    v[0].position = 0.53f;
    v[0].laneIndex = 0;
    v[0].vel = {10.0f, 0.0f};
    v[1].id = 2; // follower, lane 0 (inspect this one)
    v[1].segmentId = 0;
    v[1].position = 0.50f;
    v[1].laneIndex = 0;
    v[1].vel = {10.0f, 0.0f};
    v[2].id = 3; // beside the follower, lane 1
    v[2].segmentId = 0;
    v[2].position = 0.50f;
    v[2].laneIndex = 1;
    v[2].vel = {10.0f, 0.0f};

    Simulation sim(&net);
    sim.initialize(std::move(v));
    for(int t = 0; t < 8; ++t)
        sim.update(0.02);
    const auto snap = sim.snapshot();
    const auto acts = sim.lastActions();
    const VehicleInspection ins = sim.inspect(2);
    if(!ins.found)
    {
        std::printf("FAIL: inspect(2) not found\n");
        return false;
    }
    const Vehicle& f = snap.at(2);
    bool ok = true;
    if(ins.segmentId != f.segmentId || ins.laneIndex != f.laneIndex)
        ok = false;
    if(std::fabs(ins.obs[obs_idx::PositionAlong] - f.position) > 1e-3f)
        ok = false;
    if(std::fabs(ins.obs[obs_idx::SelfSpeed] * OBS_SPEED_SCALE - glm::length(f.vel)) > 0.5f)
        ok = false;
    // Lane-aware leader faithfulness: id1 is the same-lane car ahead.
    if(ins.leaderId != 1)
        ok = false;
    if(ins.obs[obs_idx::FrontHasLeader] <= 0.5f)
        ok = false;
    if(!(ins.obs[obs_idx::FrontGap] > 0.0f && ins.obs[obs_idx::FrontGap] < 1.0f))
        ok = false; // a real (in-range) leader gap, not free road
    // Sector faithfulness: id3 (lane 1, beside) is a side-sector neighbour.
    const SensedNeighbor& L = ins.sectors[static_cast<int>(Sector::Left)];
    const SensedNeighbor& R = ins.sectors[static_cast<int>(Sector::Right)];
    if(!((L.valid && L.id == 3) || (R.valid && R.id == 3)))
        ok = false;
    // Action faithfulness: the held action is surfaced unchanged.
    auto it = acts.find(2);
    if(it == acts.end() || it->second.accel != ins.action.accel)
        ok = false;
    return ok;
}

// Gate 14: lane-aware cross-segment leader. Topology 1->2->3; the upstream seg0 is
// single-lane, the downstream seg1 has TWO lanes. A follower nears node 2 on seg0 with
// NO same-segment leader. Downstream seg1 holds ONE stationary car, parked NEAR the node
// but in lane 1 — the lane the follower will NOT enter (it enters lane 0, clamped from
// laneIndex 0). The old any-lane front() leader would brake the follower for that lane-1
// car; the lane-aware scan sees lane 0 empty -> free road, so the follower must stay fast.
// A control run with the blocker moved into lane 0 confirms the follower DOES brake then,
// proving the filter discriminates by lane (not merely by ignoring the downstream).
struct CrossLaneResult
{
    uint64_t digest{0};
    float blockerInLane1Speed{0.0f}; // blocker in the lane ego avoids -> must stay fast
    float blockerInLane0Speed{0.0f}; // blocker in the lane ego enters -> must brake
};
static CrossLaneResult crossLaneRun()
{
    auto runWithBlockerLane = [](int blockerLane) -> std::pair<float, VehicleMap> {
        RoadNetwork net;
        auto addNode = [&](uint32_t id, float x, float y) {
            Node n;
            n.id = id;
            n.pos = {x, y};
            net.addNode(n);
        };
        addNode(1, 0, 0);
        addNode(2, 100, 0);
        addNode(3, 200, 0);
        auto addSeg = [&](uint32_t id, uint32_t f, uint32_t t, int lanes) {
            RoadSegment s;
            s.id = id;
            s.fromNode = f;
            s.toNode = t;
            s.dir = {1, 0};
            s.length = 100.0f;
            s.speedLimit = 13.9f;
            s.lanes = lanes;
            net.addSegment(s);
        };
        addSeg(0, 1, 2, 1); // upstream: single lane
        addSeg(1, 2, 3, 2); // downstream: two lanes

        std::vector<Vehicle> v;
        Vehicle follower; // lane 0 on seg0; enters lane 0 downstream (clamp of 0)
        follower.id = 1;
        follower.segmentId = 0;
        follower.position = 0.90f;
        follower.laneIndex = 0;
        follower.vel = {12.0f, 0.0f};
        v.push_back(follower);
        Vehicle blocker; // stationary, just past the node on seg1
        blocker.id = 2;
        blocker.segmentId = 1;
        blocker.position = 0.05f;
        blocker.laneIndex = static_cast<uint8_t>(blockerLane);
        blocker.vel = {0.0f, 0.0f};
        v.push_back(blocker);

        Simulation sim(&net);
        sim.initialize(std::move(v));
        VehicleMap snap;
        for(int t = 0; t < 30; ++t)
        {
            sim.update(0.02);
            snap = sim.snapshot();
        }
        return {glm::length(snap.at(1).vel), snap};
    };

    CrossLaneResult r;
    auto [lane1Speed, lane1Snap] = runWithBlockerLane(1); // blocker in the avoided lane
    auto [lane0Speed, lane0Snap] = runWithBlockerLane(0); // blocker in the entered lane
    r.blockerInLane1Speed = lane1Speed;
    r.blockerInLane0Speed = lane0Speed;

    // Digest the lane-1-blocker run (the one the new behaviour changes): a clean fork is
    // single-lane downstream, so this is the multi-lane downstream case that moves.
    std::vector<uint64_t> ids;
    for(const auto& [id, v] : lane1Snap)
    {
        (void)v;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](uint64_t x) {
        h ^= x;
        h *= 1099511628211ULL;
    };
    for(uint64_t id : ids)
    {
        const Vehicle& v = lane1Snap.at(id);
        const float sp = glm::length(v.vel);
        mix(id);
        mix(v.segmentId);
        mix(static_cast<uint64_t>(v.laneIndex));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(v.position * 1e6))));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(sp * 1e3))));
    }
    r.digest = h;
    return r;
}

// Gate 16: two-way roads. A one-way segment A->B is made two-way; assert the reverse
// segment is synthesized + cross-linked, both directions route, the median separates the
// opposing directions in world space, and a 2-vehicle run is finite + deterministic.
struct TwoWayResult
{
    uint64_t digest{0};
    float ySep{0.0f};
    bool paired{false};
    bool routed{false};
    bool finite{true};
};
static TwoWayResult twoWayRun()
{
    RoadNetwork net;
    Node a, b;
    a.id = 1;
    a.pos = {0, 0};
    b.id = 2;
    b.pos = {1000, 0};
    net.addNode(a);
    net.addNode(b);
    RoadSegment s{};
    s.id = 10;
    s.fromNode = 1;
    s.toNode = 2;
    s.dir = {1, 0};
    s.length = 1000.0f;
    s.lanes = 1;
    s.speedLimit = 13.9f;
    net.addSegment(s);
    const uint32_t revId = net.makeTwoWay(10, 3.5f, 3.5f); // halfMedian = 0.5*1*3.5 + 0.5*3.5 = 3.5

    TwoWayResult r;
    const RoadSegment* fwd = net.getSegment(10);
    const RoadSegment* rev = net.getSegment(revId);
    r.paired = (revId != 0 && revId != 10 && fwd && rev && fwd->pairId == revId &&
                rev->pairId == 10 && rev->fromNode == 2 && rev->toNode == 1);
    r.routed = !net.route(1, 2).empty() && !net.route(2, 1).empty(); // both directions traversable

    Simulation sim(&net);
    std::vector<Vehicle> v(2);
    v[0].id = 1;
    v[0].segmentId = 10;
    v[0].position = 0.5f;
    v[0].laneIndex = 0;
    v[0].vel = {10.0f, 0.0f};
    v[1].id = 2;
    v[1].segmentId = revId;
    v[1].position = 0.5f;
    v[1].laneIndex = 0;
    v[1].vel = {10.0f, 0.0f};
    sim.initialize(std::move(v));
    {
        const auto s0 = sim.snapshot();
        r.ySep = std::fabs(s0.at(1).worldPos.y - s0.at(2).worldPos.y); // ~2*halfMedian = 7
    }
    VehicleMap snap;
    for(int t = 0; t < 60; ++t)
    {
        sim.update(0.02);
        snap = sim.snapshot();
        for(const auto& [id, vv] : snap)
        {
            (void)id;
            if(!std::isfinite(vv.worldPos.x) || !std::isfinite(vv.worldPos.y))
                r.finite = false;
        }
    }
    std::vector<uint64_t> ids;
    for(const auto& [id, vv] : snap)
    {
        (void)vv;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](uint64_t x) {
        h ^= x;
        h *= 1099511628211ULL;
    };
    for(uint64_t id : ids)
    {
        const Vehicle& vv = snap.at(id);
        const float sp = glm::length(vv.vel);
        mix(id);
        mix(vv.segmentId);
        mix(static_cast<uint64_t>(vv.laneIndex));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(vv.position * 1e6))));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(sp * 1e3))));
    }
    r.digest = h;
    return r;
}

int main(int argc, char** argv)
{
    bool printOnly = (argc > 1 && std::strcmp(argv[1], "--print-hash") == 0);
    int failures = 0;

    // Gate 1: route() correctness (digest-independent — guards the BFS fix directly).
    {
        RoadNetwork net = makeScenarioNetwork();
        auto r13 = net.route(1, 3); // 1->2->3 via seg0, seg1
        auto r24 = net.route(2, 4); // 2->4 via seg3 (the fork)
        const std::vector<uint32_t> exp13{0, 1};
        const std::vector<uint32_t> exp24{3};
        if(r13 != exp13)
        {
            std::printf("FAIL: route(1,3) wrong size=%zu\n", r13.size());
            ++failures;
        }
        if(r24 != exp24)
        {
            std::printf("FAIL: route(2,4) wrong size=%zu\n", r24.size());
            ++failures;
        }
    }

    RunResult a = runOnce();
    RunResult b = runOnce();

    std::printf("digest run1 = 0x%016llx\n", (unsigned long long)a.digest);
    std::printf("digest run2 = 0x%016llx\n", (unsigned long long)b.digest);
    std::printf("maxSpeed=%.3f free=%.3f sawStop=%d nonFinite=%d\n", a.maxSpeed,
                a.freeVehicleSpeed, a.sawStop ? 1 : 0, a.anyNonFinite ? 1 : 0);

    MultiLaneResult ml = multiLaneRun();
    std::printf("multilane digest = 0x%016llx (laneChange=%d overtook=%d signal=%d overlap=%d)\n",
                (unsigned long long)ml.digest, ml.sawLaneChange ? 1 : 0, ml.overtook ? 1 : 0,
                ml.sawSignal ? 1 : 0, ml.overlap ? 1 : 0);

    NNResult nn = nnRun();
    std::printf("nn digest = 0x%016llx (finite=%d bounded=%d overlap=%d maxSpeed=%.3f)\n",
                (unsigned long long)nn.digest, nn.finite ? 1 : 0, nn.bounded ? 1 : 0,
                nn.overlap ? 1 : 0, nn.maxSpeed);

    NNResult nnp = nnRun(4); // bank of 4 heterogeneous nets
    std::printf("nn-pop digest = 0x%016llx (finite=%d bounded=%d overlap=%d) [!= nn: %d]\n",
                (unsigned long long)nnp.digest, nnp.finite ? 1 : 0, nnp.bounded ? 1 : 0,
                nnp.overlap ? 1 : 0, (nnp.digest != nn.digest) ? 1 : 0);

    VtResult vtres = vtableRun();
    std::printf("vtable digest = 0x%016llx (abiOk=%d finite=%d bounded=%d)\n",
                (unsigned long long)vtres.digest, vtres.abiOk ? 1 : 0, vtres.finite ? 1 : 0,
                vtres.bounded ? 1 : 0);

    CsvResult csv = trajectoryCsvRun();
    std::printf("trajectory csv digest = 0x%016llx (state digest = 0x%016llx)\n",
                (unsigned long long)csv.csvDigest, (unsigned long long)csv.stateDigest);

    CrossLaneResult cl = crossLaneRun();
    std::printf("cross-lane digest = 0x%016llx (blockerLane1=%.3f blockerLane0=%.3f)\n",
                (unsigned long long)cl.digest, cl.blockerInLane1Speed, cl.blockerInLane0Speed);

    TwoWayResult tw = twoWayRun();
    std::printf("two-way digest = 0x%016llx (paired=%d routed=%d ySep=%.2f finite=%d)\n",
                (unsigned long long)tw.digest, tw.paired ? 1 : 0, tw.routed ? 1 : 0, tw.ySep,
                tw.finite ? 1 : 0);

    if(printOnly)
        return 0;

    if(a.digest != b.digest)
    {
        std::printf("FAIL: non-deterministic — run1 != run2\n");
        ++failures;
    }
    if(kGolden != 0ULL && a.digest != kGolden)
    {
        std::printf("FAIL: digest 0x%016llx != golden 0x%016llx\n", (unsigned long long)a.digest,
                    (unsigned long long)kGolden);
        ++failures;
    }
    if(kGolden == 0ULL)
        std::printf("NOTE: golden digest not pinned (kGolden==0); skipping golden check.\n");

    if(a.anyNonFinite)
    {
        std::printf("FAIL: non-finite vehicle state\n");
        ++failures;
    }
    if(a.maxSpeed > 13.9f + 1.0f)
    {
        std::printf("FAIL: max speed %.3f exceeds desired-speed cap\n", a.maxSpeed);
        ++failures;
    }
    if(a.freeVehicleSpeed < 5.0f)
    {
        std::printf("FAIL: free vehicle did not accelerate (speed %.3f)\n", a.freeVehicleSpeed);
        ++failures;
    }
    if(!a.sawStop)
    {
        std::printf("FAIL: no vehicle stopped at the STOP sign\n");
        ++failures;
    }
    if(!a.leaderProceeded)
    {
        std::printf("FAIL: the stopping leader never proceeded (STOP-sign deadlock)\n");
        ++failures;
    }
    if(!a.sawLightStop)
    {
        std::printf("FAIL: vehicle 4 never stopped at the red light\n");
        ++failures;
    }
    if(!a.lightProceeded)
    {
        std::printf("FAIL: vehicle 4 never proceeded through the light (green never released)\n");
        ++failures;
    }
    if(!crossSegmentGate())
    {
        std::printf("FAIL: cross-segment leader did not make the follower brake\n");
        ++failures;
    }
    if(a.anyLaneChange)
    {
        std::printf("FAIL: a vehicle changed lanes on the single-lane network (MOBIL not a no-op)\n");
        ++failures;
    }
    {
        MultiLaneResult ml2 = multiLaneRun();
        if(ml.digest != ml2.digest)
        {
            std::printf("FAIL: multi-lane scenario non-deterministic\n");
            ++failures;
        }
        if(kGoldenMultiLane != 0ULL && ml.digest != kGoldenMultiLane)
        {
            std::printf("FAIL: multi-lane digest 0x%016llx != golden 0x%016llx\n",
                        (unsigned long long)ml.digest, (unsigned long long)kGoldenMultiLane);
            ++failures;
        }
        if(!ml.sawLaneChange)
        {
            std::printf("FAIL: follower never changed lanes to overtake\n");
            ++failures;
        }
        if(!ml.overtook)
        {
            std::printf("FAIL: follower never overtook the slow truck\n");
            ++failures;
        }
        if(ml.overlap)
        {
            std::printf("FAIL: two vehicles overlapped in the same lane\n");
            ++failures;
        }
        if(!ml.sawSignal)
        {
            std::printf("FAIL: no turn signal observed during the lane change\n");
            ++failures;
        }
    }
    {
        NNResult nn2 = nnRun();
        if(nn.digest != nn2.digest)
        {
            std::printf("FAIL: nn brain non-deterministic\n");
            ++failures;
        }
        if(kGoldenNN != 0ULL && nn.digest != kGoldenNN)
        {
            std::printf("FAIL: nn digest 0x%016llx != golden 0x%016llx\n",
                        (unsigned long long)nn.digest, (unsigned long long)kGoldenNN);
            ++failures;
        }
        if(!nn.finite)
        {
            std::printf("FAIL: nn brain produced non-finite state\n");
            ++failures;
        }
        if(!nn.bounded)
        {
            std::printf("FAIL: nn brain speed exceeded bounds (validator/integrator)\n");
            ++failures;
        }
        if(nn.overlap)
        {
            std::printf("FAIL: nn brain caused a same-lane overlap (Phase-B0 gap re-check)\n");
            ++failures;
        }
    }
    // Gate 15: per-vehicle NN bank (population>1) — heterogeneous but still safe +
    // deterministic. NOTE: kGoldenNNPop is coupled to the vehicle-id->member assignment
    // (splitmix64(id)%K), so a future change to the id-allocation scheme is expected to
    // re-pin this gate — but MUST leave Gate 10/kGoldenNN and the 5 originals byte-identical.
    {
        NNResult nnp2 = nnRun(4);
        if(nnp.digest != nnp2.digest)
        {
            std::printf("FAIL: nn bank (population=4) non-deterministic\n");
            ++failures;
        }
        if(nnp.digest == kGoldenNN)
        {
            std::printf("FAIL: nn bank population=4 did not diverge from the shared net\n");
            ++failures;
        }
        if(!nnp.finite || !nnp.bounded || nnp.overlap)
        {
            std::printf("FAIL: nn bank produced unsafe state (finite=%d bounded=%d overlap=%d)\n",
                        nnp.finite ? 1 : 0, nnp.bounded ? 1 : 0, nnp.overlap ? 1 : 0);
            ++failures;
        }
        if(kGoldenNNPop != 0ULL && nnp.digest != kGoldenNNPop)
        {
            std::printf("FAIL: nn-pop digest 0x%016llx != golden 0x%016llx\n",
                        (unsigned long long)nnp.digest, (unsigned long long)kGoldenNNPop);
            ++failures;
        }
    }
    {
        VtResult vt2 = vtableRun();
        if(!vtres.abiOk)
        {
            std::printf("FAIL: vtable ABI gate rejected a valid descriptor/vtable\n");
            ++failures;
        }
        if(vtres.digest != vt2.digest)
        {
            std::printf("FAIL: vtable-brain adapter non-deterministic\n");
            ++failures;
        }
        if(kGoldenVtable != 0ULL && vtres.digest != kGoldenVtable)
        {
            std::printf("FAIL: vtable digest 0x%016llx != golden 0x%016llx\n",
                        (unsigned long long)vtres.digest, (unsigned long long)kGoldenVtable);
            ++failures;
        }
        if(!vtres.finite || !vtres.bounded)
        {
            std::printf("FAIL: vtable-brain produced non-finite/unbounded state\n");
            ++failures;
        }
    }
    {
        if(csv.stateDigest != kGolden)
        {
            std::printf("FAIL: trajectory export perturbed the sim (state 0x%016llx != golden)\n",
                        (unsigned long long)csv.stateDigest);
            ++failures;
        }
        CsvResult csv2 = trajectoryCsvRun();
        if(csv.csvDigest != csv2.csvDigest)
        {
            std::printf("FAIL: trajectory CSV non-deterministic\n");
            ++failures;
        }
        if(kGoldenTrajectoryCsv != 0ULL && csv.csvDigest != kGoldenTrajectoryCsv)
        {
            std::printf("FAIL: trajectory CSV digest 0x%016llx != golden 0x%016llx\n",
                        (unsigned long long)csv.csvDigest, (unsigned long long)kGoldenTrajectoryCsv);
            ++failures;
        }
    }
    if(!inspectGate())
    {
        std::printf("FAIL: inspect() disagreed with the vehicle's real state/action\n");
        ++failures;
    }
    {
        CrossLaneResult cl2 = crossLaneRun();
        if(cl.digest != cl2.digest)
        {
            std::printf("FAIL: cross-lane scenario non-deterministic\n");
            ++failures;
        }
        if(kGoldenCrossLane != 0ULL && cl.digest != kGoldenCrossLane)
        {
            std::printf("FAIL: cross-lane digest 0x%016llx != golden 0x%016llx\n",
                        (unsigned long long)cl.digest, (unsigned long long)kGoldenCrossLane);
            ++failures;
        }
        if(kGoldenCrossLane == 0ULL)
            std::printf("NOTE: cross-lane digest not pinned (kGoldenCrossLane==0); skipping.\n");
        // Lane-aware behaviour: a blocker in the lane the ego will NOT enter must not slow
        // it (free road downstream); a blocker in the lane it WILL enter must brake it.
        if(cl.blockerInLane1Speed < cl.blockerInLane0Speed + 2.0f)
        {
            std::printf("FAIL: cross-seg leader not lane-aware (lane1=%.3f lane0=%.3f)\n",
                        cl.blockerInLane1Speed, cl.blockerInLane0Speed);
            ++failures;
        }
    }
    { // Gate 16: two-way road synthesis + median separation + determinism
        if(!tw.paired)
        {
            std::printf("FAIL: two-way reverse segment not synthesized / not cross-linked\n");
            ++failures;
        }
        if(!tw.routed)
        {
            std::printf("FAIL: two-way road not routable in both directions\n");
            ++failures;
        }
        if(tw.ySep < 6.0f) // expect ~2*halfMedian = 7
        {
            std::printf("FAIL: two-way median did not separate opposing directions (ySep=%.2f)\n",
                        tw.ySep);
            ++failures;
        }
        if(!tw.finite)
        {
            std::printf("FAIL: two-way run produced non-finite state\n");
            ++failures;
        }
        TwoWayResult tw2 = twoWayRun();
        if(tw.digest != tw2.digest)
        {
            std::printf("FAIL: two-way scenario non-deterministic\n");
            ++failures;
        }
        if(kGoldenTwoWay != 0ULL && tw.digest != kGoldenTwoWay)
        {
            std::printf("FAIL: two-way digest 0x%016llx != golden 0x%016llx\n",
                        (unsigned long long)tw.digest, (unsigned long long)kGoldenTwoWay);
            ++failures;
        }
    }

    if(failures == 0)
        std::printf("PASS: golden_trajectory (16 gates)\n");
    return failures == 0 ? 0 : 1;
}
