#ifndef TFV_RUN_MANIFEST_HPP
#define TFV_RUN_MANIFEST_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>

namespace tfv
{
    /**
     * Reproducibility manifest for one simulation run (Phase 7). Serialized as
     * dependency-free JSON. With the master seed + config snapshot + brain
     * kind/weights-hash + the input data files, a researcher can re-create the run on
     * the same build; final_state_digest confirms a byte-identical replay (statistical/
     * same-build tier). wall_clock_utc is the only non-deterministic field — it is
     * injected by the caller and is NEVER fed into any digest.
     */
    struct RunManifest
    {
        int schemaVersion{1};
        uint64_t masterSeed{0};
        std::string brainKind{"none"};
        std::string brainWeightsHash{"n/a"};
        std::string cityFile, vehiclesFile, signsFile;
        std::size_t vehicleCount{0};
        double decisionHz{0.0};
        double fixedDt{0.0};
        uint64_t totalTicks{0};
        int exportEveryN{1};
        std::string gitSha{"unknown"};
        std::string wallClockUtc;        // injected; NEVER hashed
        uint64_t finalStateDigest{0};    // FNV-1a over the final state (same mix as golden)
        std::map<std::string, std::string> configSnapshot; // sorted sim.* / perf.* keys

        void write(std::ostream& os) const; // emit JSON
    };

} // namespace tfv

#endif // TFV_RUN_MANIFEST_HPP
