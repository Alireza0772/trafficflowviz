// Golden-trajectory test for the Phase 2 agent (IDM) simulation.
//
// Standalone, headless (no SDL/Engine/rendering), hermetic (the road network and
// vehicles are built programmatically; Configuration is NOT initialized so the
// hardcoded getter defaults apply and no stray .conf file can perturb the run).
//
// Three gates:
//   1) run-twice determinism  : two in-process runs must produce the same digest
//                               (the statistical / same-build reproducibility tier).
//   2) golden digest          : digest must match kGolden (regenerate with --print-hash
//                               after an intentional model change; environment-pinned).
//   3) IDM invariants         : no NaN/Inf; speeds bounded by v0_cap; a free vehicle
//                               accelerates away from rest.

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
static constexpr uint64_t kGolden = 0x5b0ba96daf7f1316ULL;

namespace
{
    // A small deterministic two-segment loop: node0 --seg0--> node1 --seg1--> node0.
    RoadNetwork makeScenarioNetwork()
    {
        RoadNetwork net;

        Node n0;
        n0.id = 0;
        n0.pos = {0.0f, 0.0f};
        n0.outgoing = {0}; // seg0 starts here
        Node n1;
        n1.id = 1;
        n1.pos = {100.0f, 0.0f};
        n1.outgoing = {1}; // seg1 starts here
        net.addNode(n0);
        net.addNode(n1);

        RoadSegment s0;
        s0.id = 0;
        s0.fromNode = 0;
        s0.toNode = 1;
        s0.length = 100.0f;
        s0.speedLimit = 13.9f;
        s0.dir = {1.0f, 0.0f};
        RoadSegment s1;
        s1.id = 1;
        s1.fromNode = 1;
        s1.toNode = 0;
        s1.length = 100.0f;
        s1.speedLimit = 13.9f;
        s1.dir = {-1.0f, 0.0f};
        net.addSegment(s0);
        net.addSegment(s1);

        return net;
    }

    std::vector<Vehicle> makeScenarioVehicles()
    {
        std::vector<Vehicle> v(3);
        // Slow leader on seg0, ahead of the follower.
        v[0].id = 1;
        v[0].segmentId = 0;
        v[0].position = 0.6f;
        v[0].vel = {2.0f, 0.0f};
        // Fast follower behind the leader on seg0 (must brake).
        v[1].id = 2;
        v[1].segmentId = 0;
        v[1].position = 0.2f;
        v[1].vel = {10.0f, 0.0f};
        // Free vehicle starting at rest on seg1 (must accelerate toward v0).
        v[2].id = 3;
        v[2].segmentId = 1;
        v[2].position = 0.3f;
        v[2].vel = {0.0f, 0.0f};
        return v;
    }

    struct RunResult
    {
        uint64_t digest{0};
        float maxSpeed{0.0f};
        float freeVehicleSpeed{0.0f};
        bool anyNonFinite{false};
    };

    RunResult runOnce()
    {
        RoadNetwork net = makeScenarioNetwork();
        Simulation sim(&net);
        sim.initialize(makeScenarioVehicles());

        const double dt = 0.02; // 50 Hz
        for(int t = 0; t < 600; ++t)
            sim.update(dt);

        VehicleMap snap = sim.snapshot();

        std::vector<uint64_t> ids;
        ids.reserve(snap.size());
        for(const auto& [id, v] : snap)
            ids.push_back(id);
        std::sort(ids.begin(), ids.end());

        // FNV-1a/64 over quantized state (quantization absorbs last-bit FP jitter).
        uint64_t h = 1469598103934665603ULL;
        auto mix = [&](uint64_t x) {
            h ^= x;
            h *= 1099511628211ULL;
        };

        RunResult r;
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

    RunResult a = runOnce();
    RunResult b = runOnce();

    std::printf("digest run1 = 0x%016llx\n", (unsigned long long)a.digest);
    std::printf("digest run2 = 0x%016llx\n", (unsigned long long)b.digest);
    std::printf("maxSpeed=%.3f  freeVehicleSpeed=%.3f  nonFinite=%d\n", a.maxSpeed,
                a.freeVehicleSpeed, a.anyNonFinite ? 1 : 0);

    if(printOnly)
        return 0;

    int failures = 0;

    // Gate 1: run-twice determinism (always valid for the same build).
    if(a.digest != b.digest)
    {
        std::printf("FAIL: non-deterministic — run1 != run2\n");
        ++failures;
    }

    // Gate 2: committed golden digest (skipped until pinned).
    if(kGolden != 0ULL && a.digest != kGolden)
    {
        std::printf("FAIL: digest 0x%016llx != golden 0x%016llx\n", (unsigned long long)a.digest,
                    (unsigned long long)kGolden);
        ++failures;
    }
    if(kGolden == 0ULL)
        std::printf("NOTE: golden digest not pinned (kGolden==0); skipping golden check.\n");

    // Gate 3: IDM behavioural invariants (independent of the exact digest).
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

    if(failures == 0)
        std::printf("PASS: golden_trajectory (%d gates)\n", 3);
    return failures == 0 ? 0 : 1;
}
