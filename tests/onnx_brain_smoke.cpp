// Build-gated smoke test for the ONNX Runtime brain backend (only built/run when
// TFV_WITH_ONNX=ON). ONNX is same-build/same-ENVIRONMENT only, so this asserts
// run-twice equality WITHIN one process + finite + bounded — NEVER a pinned cross-machine
// digest. A model is supplied via the env var TFV_ONNX_TEST_MODEL; if unset/absent the
// test SKIPs (the model-independent rule-fallback assertion still runs). The 11-gate
// golden test stays ONNX-free.

#include "agents/BrainRegistry.hpp"
#include "agents/IdmParams.hpp"
#include "agents/OnnxBrain.hpp"
#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "core/TrafficEntity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace tfv;

static uint64_t runWith(std::unique_ptr<IBrain> brain, bool& finite, bool& bounded)
{
    RoadNetwork net;
    Node n1, n2;
    n1.id = 1;
    n1.pos = {0, 0};
    n2.id = 2;
    n2.pos = {1500, 0};
    net.addNode(n1);
    net.addNode(n2);
    RoadSegment s;
    s.id = 0;
    s.fromNode = 1;
    s.toNode = 2;
    s.dir = {1, 0};
    s.length = 1500.0f;
    s.speedLimit = 13.9f;
    s.lanes = 2;
    net.addSegment(s);
    std::vector<Vehicle> v(3);
    for(int i = 0; i < 3; ++i)
    {
        v[i].id = static_cast<uint64_t>(i + 1);
        v[i].segmentId = 0;
        v[i].position = 0.15f + 0.1f * i;
        v[i].vel = {5.0f, 0.0f};
    }
    Simulation sim(&net);
    sim.initialize(std::move(v));
    sim.setBrainForTest(std::move(brain));

    IdmParams idm;
    VehicleMap snap;
    finite = true;
    bounded = true;
    for(int t = 0; t < 400; ++t)
    {
        sim.update(0.02);
        snap = sim.snapshot();
        for(const auto& [id, vv] : snap)
        {
            (void)id;
            const float sp = glm::length(vv.vel);
            if(!std::isfinite(sp) || !std::isfinite(vv.position))
                finite = false;
            if(sp < -0.01f || sp > idm.v0_cap + 1.0f)
                bounded = false;
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
        mix(static_cast<uint64_t>(vv.laneIndex));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(vv.position * 1e6))));
        mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(glm::length(vv.vel) * 1e3))));
    }
    return h;
}

int main()
{
    IdmParams idm;
    int failures = 0;

    // Model-independent: a missing model must fall back to a usable rule brain.
    {
        auto fb = makeBrain("onnx:/no/such/model.onnx", idm);
        if(!fb || std::string(fb->kindName()) != "rule")
        {
            std::printf("FAIL: missing onnx model did not fall back to rule\n");
            ++failures;
        }
    }

    const char* modelPath = std::getenv("TFV_ONNX_TEST_MODEL");
    if(!modelPath || !*modelPath)
    {
        std::printf("SKIP: TFV_ONNX_TEST_MODEL not set (no model to run end-to-end)\n");
        return failures == 0 ? 0 : 1;
    }

    const std::string kind = std::string("onnx:") + modelPath;
    auto probe = makeBrain(kind, idm);
    if(!probe || std::string(probe->kindName()) != "onnx")
    {
        std::printf("FAIL: model did not load as an onnx brain (%s)\n", modelPath);
        return 1;
    }

    bool f1 = true, b1 = true, f2 = true, b2 = true;
    const uint64_t d1 = runWith(makeBrain(kind, idm), f1, b1);
    const uint64_t d2 = runWith(makeBrain(kind, idm), f2, b2);
    if(!f1 || !b1)
    {
        std::printf("FAIL: onnx brain not finite/bounded\n");
        ++failures;
    }
    if(d1 != d2)
    {
        std::printf("FAIL: onnx brain non-deterministic within process\n");
        ++failures;
    }

    if(failures == 0)
        std::printf("PASS: onnx_brain_smoke\n");
    return failures == 0 ? 0 : 1;
}
