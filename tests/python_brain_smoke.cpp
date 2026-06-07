// Build-gated smoke test for the embedded-Python brain backend (only built/run when
// TFV_WITH_PYTHON=ON). Python is same-build/same-ENVIRONMENT only, so this asserts
// run-twice equality WITHIN one process + finite + bounded + fail-soft — NEVER a pinned
// cross-machine digest. The 11-gate golden test stays Python-free.

#include "agents/BrainRegistry.hpp"
#include "agents/IdmParams.hpp"
#include "agents/PythonBrain.hpp"
#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "core/TrafficEntity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace tfv;

static const char* kTmpDir = "/tmp/tfv_py_smoke";

static void writeModule(const std::string& name, const std::string& body)
{
    std::system((std::string("mkdir -p ") + kTmpDir).c_str());
    std::ofstream f(std::string(kTmpDir) + "/" + name + ".py");
    f << body;
}

// Run a 2-lane scenario with `brain` for 400 ticks; report finite/bounded; return a
// digest of the final state (id/segment/lane/position/speed).
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
    const std::string path = std::string("path=") + kTmpDir;

    writeModule("tfv_smoke_cruise", "import numpy as np\n"
                                    "def decide_batch(obs):\n"
                                    "    speed = obs[:,0]*40.0\n"
                                    "    v0 = obs[:,4]*40.0\n"
                                    "    a = np.clip((v0-speed)*0.4, -6.0, 1.5).astype(np.float32)\n"
                                    "    return a.reshape(-1,1)\n");
    writeModule("tfv_smoke_bad", "def decide_batch(obs):\n"
                                 "    raise ValueError('boom')\n");

    auto mkCruise = [&]() {
        return loadPythonBrain("tfv_smoke_cruise", "decide_batch", path, idm);
    };

    // (1) The cruise brain loads, runs finite + bounded, and is run-twice deterministic.
    auto b1 = mkCruise();
    if(!b1)
    {
        std::printf("FAIL: cruise python brain failed to load\n");
        return 1;
    }
    bool f1 = true, b1bounded = true, f2 = true, b2bounded = true;
    const uint64_t d1 = runWith(std::move(b1), f1, b1bounded);
    const uint64_t d2 = runWith(mkCruise(), f2, b2bounded);
    if(!f1 || !b1bounded)
    {
        std::printf("FAIL: cruise brain not finite/bounded\n");
        ++failures;
    }
    if(d1 != d2)
    {
        std::printf("FAIL: python brain non-deterministic within process (0x%llx != 0x%llx)\n",
                    (unsigned long long)d1, (unsigned long long)d2);
        ++failures;
    }

    // (2) A module whose decide_batch raises must FAIL-SOFT: it still loads (warm-up is
    //     soft) and holds (zero accel), keeping the sim finite/bounded and alive.
    auto bad = loadPythonBrain("tfv_smoke_bad", "decide_batch", path, idm);
    if(!bad)
    {
        std::printf("FAIL: throwing module should still load (warm-up is soft)\n");
        ++failures;
    }
    else
    {
        bool f3 = true, b3 = true;
        runWith(std::move(bad), f3, b3);
        if(!f3 || !b3)
        {
            std::printf("FAIL: throwing brain was not fail-soft\n");
            ++failures;
        }
    }

    // (3) A missing module via makeBrain falls back to a usable rule brain.
    auto fb = makeBrain("python:tfv_does_not_exist", idm);
    if(!fb || std::string(fb->kindName()) != "rule")
    {
        std::printf("FAIL: missing python module did not fall back to rule\n");
        ++failures;
    }

    if(failures == 0)
        std::printf("PASS: python_brain_smoke\n");
    return failures == 0 ? 0 : 1;
}
