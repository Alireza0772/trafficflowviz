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

#include "core/Configuration.hpp"
#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "core/TrafficEntity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <glm/glm.hpp>
#include <vector>

using namespace tfv;

// Committed reference digest (0 = not yet pinned; run with --print-hash to obtain).
static constexpr uint64_t kGolden = 0x1be470af7fcd83faULL;

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
        std::vector<Vehicle> v(3);
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

    if(failures == 0)
        std::printf("PASS: golden_trajectory (5 gates)\n");
    return failures == 0 ? 0 : 1;
}
