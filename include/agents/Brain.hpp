#ifndef TFV_BRAIN_HPP
#define TFV_BRAIN_HPP

#include "agents/Action.hpp"
#include "agents/Observation.hpp"

#include <cstdint>
#include <string>

namespace tfv
{
    /**
     * The decision seam: a vehicle (or, later, the central light controller) maps a
     * fixed-length Observation to an Action. This in-process C++ interface mirrors
     * the Phase 6 stable C-ABI vtable so external brains (Python / ONNX / DLL) can
     * be wrapped behind it without changing the Simulation call site:
     *
     *   C ABI (Phase 6):  void decide_batch(void* self, const float* obs, int n,
     *                                        int obsLen, struct Action* out)
     *
     * decideBatch is intentionally BATCHED now (N agents -> N actions over
     * contiguous arrays) so the per-tick call shape is final: Python/ONNX backends
     * amortize GIL/FFI cost over the batch. RuleBrain simply loops internally.
     */
    class IBrain
    {
      public:
        virtual ~IBrain() = default;

        /** Decide for n agents. obs and out are caller-owned, length n. */
        virtual void decideBatch(const Observation* obs, int n, Action* out) = 0;

        /** Id-aware batch decide: same as decideBatch but the per-agent vehicle ids are
         *  available, so a brain can select per-vehicle weights (heterogeneous agents).
         *  The default forwards to decideBatch (ids ignored), so existing brains are
         *  unaffected. The Simulation calls this; obs/ids/out are length n. */
        virtual void decideBatchIds(const Observation* obs, const uint64_t* ids, int n, Action* out)
        {
            (void)ids;
            decideBatch(obs, n, out);
        }

        /** Re-seed any internal RNG/state for reproducible (re-)initialization. */
        virtual void reset(uint64_t seed) = 0;

        /** Content tag of the brain's weights/config (-> reproducibility manifest). */
        virtual std::string weightsHash() const { return "n/a"; }

        /** Short kind name, e.g. "rule". */
        virtual const char* kindName() const = 0;

        /** True if this brain emits its own Action.laneChange (so the Simulation honors
         *  it instead of the built-in MOBIL evaluator). Default false (rule brain). */
        virtual bool drivesLaneChange() const { return false; }

        /** Convenience single-agent decide (for tests/oracles). */
        Action decide(const Observation& obs)
        {
            Action a{};
            decideBatch(&obs, 1, &a);
            return a;
        }
    };

} // namespace tfv

#endif // TFV_BRAIN_HPP
