// tfv_headless — run the simulation headless (no SDL window) and export a reproducible
// run: a RunManifest (JSON) + per-tick trajectory CSV + per-segment metrics CSV. For
// batch research runs. Reuses the SAME exporters as the GUI path.
//
//   tfv_headless                       # uses trafficflowviz.conf defaults
//   tfv_headless --export-ticks=1200 --export-dir=out --sim-default-brain=nn
//   (any --key=value maps to a config key, '-' -> '.', as Configuration parses)

#include "core/Configuration.hpp"
#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "core/TrafficEntity.hpp"
#include "io/MetricsExporter.hpp"
#include "io/RunManifest.hpp"
#include "io/TrajectoryExporter.hpp"
#include "utils/LoggingManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#ifndef TFV_GIT_SHA
#define TFV_GIT_SHA "unknown"
#endif

using namespace tfv;

namespace
{
    std::string isoUtcNow()
    {
        std::time_t t = std::time(nullptr);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
        return buf;
    }

    // FNV-1a over the final state (same mix as the golden test): reproducibility check.
    uint64_t stateDigest(const VehicleMap& snap)
    {
        std::vector<uint64_t> ids;
        for(const auto& [id, v] : snap)
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
            const Vehicle& v = snap.at(id);
            const float sp = glm::length(v.vel);
            mix(id);
            mix(v.segmentId);
            mix(static_cast<uint64_t>(v.laneIndex));
            mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(v.position * 1e6))));
            mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(sp * 1e3))));
        }
        return h;
    }
} // namespace

int main(int argc, char** argv)
{
    auto& cfg = TFV_CONFIG();
    cfg.initialize(std::nullopt, argc, argv); // trafficflowviz.conf + --key=value overrides

    const std::string cityFile = cfg.getString("data.city_file", "roads/roads_complex.csv");
    const std::string vehiclesFile = cfg.getString("data.vehicles_file", "vehicles/vehicles.csv");
    const std::string signsFile = cfg.getString("data.signs_file", "roads/signs.csv");
    const int ticks = std::max(1, cfg.getInt("export.ticks", 600));
    const int everyN = std::max(1, cfg.getInt("sim.export.every_n", 1));
    const std::string outDir = cfg.getString("export.dir", ".");
    double dt = cfg.getSimulationTimeStep();
    if(dt <= 0.0)
        dt = 0.02;

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    // Default-construct (no road network) so initialize() loads the CSV itself, exactly
    // as the Engine does; passing an empty network would suppress the load.
    Simulation sim;
    if(!sim.initialize(cfg.getCityDataPath(), cfg.getVehicleDataPath()))
    {
        LOG_ERROR("[headless] simulation init failed");
        return 1;
    }

    TrajectoryExporter traj((std::filesystem::path(outDir) / "trajectory.csv").string(), everyN);
    MetricsExporter metrics((std::filesystem::path(outDir) / "metrics.csv").string(), everyN);
    LOG_INFO("[headless] {n} vehicles, brain '{b}', {t} ticks @ dt={dt}",
             PARAM(n, sim.vehicleCount()), PARAM(b, sim.brainKind()), PARAM(t, ticks),
             PARAM(dt, dt));

    for(int t = 0; t < ticks; ++t)
    {
        sim.update(dt);
        traj.sample(static_cast<uint64_t>(t), t * dt, sim);
        metrics.sample(static_cast<uint64_t>(t), t * dt, sim);
    }

    RunManifest m;
    m.masterSeed = cfg.getMasterSeed();
    m.brainKind = sim.brainKind();
    m.brainWeightsHash = sim.brainWeightsHash();
    m.cityFile = cityFile;
    m.vehiclesFile = vehiclesFile;
    m.signsFile = signsFile;
    m.vehicleCount = sim.vehicleCount();
    m.decisionHz = cfg.getFloat("perf.decision_hz", 10.0f);
    m.fixedDt = dt;
    m.totalTicks = static_cast<uint64_t>(ticks);
    m.exportEveryN = everyN;
    m.gitSha = TFV_GIT_SHA;
    m.wallClockUtc = isoUtcNow(); // the only non-deterministic field; never hashed
    m.finalStateDigest = stateDigest(sim.snapshot());
    for(const auto& [k, v] : cfg.snapshot("sim."))
        m.configSnapshot[k] = v;
    for(const auto& [k, v] : cfg.snapshot("perf."))
        m.configSnapshot[k] = v;

    std::ofstream mf((std::filesystem::path(outDir) / "manifest.json").string());
    m.write(mf);

    char hexDigest[24];
    std::snprintf(hexDigest, sizeof(hexDigest), "0x%016llx",
                  static_cast<unsigned long long>(m.finalStateDigest));
    LOG_INFO("[headless] wrote manifest.json + trajectory.csv + metrics.csv to {dir} "
             "(final digest {d})",
             PARAM(dir, outDir), PARAM(d, std::string(hexDigest)));
    return 0;
}
