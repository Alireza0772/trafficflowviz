#ifndef TFV_ACTION_VALIDATOR_HPP
#define TFV_ACTION_VALIDATOR_HPP

#include "agents/Action.hpp"
#include "agents/IdmParams.hpp"

#include <cstdint>

namespace tfv
{
    /**
     * Deterministic, RNG-free validator that turns a brain's Action REQUEST into a
     * physically/structurally valid Action in place, returning the number of fields it
     * had to fix (the per-agent "violation" count, exposed via the Simulation).
     *
     * On an already-valid Action (the rule brain: accel pre-clamped by IDM,
     * laneChange/turn 0, lightCmd within the defined bits) it is a BYTE-IDENTICAL
     * no-op returning 0 — so it never perturbs the rule-brain golden digests.
     *
     * Lateral SAFETY (a feasible-but-unsafe lane change) is enforced separately by the
     * Phase-B0 gap re-check; that is traffic, not a malformed request, so it is not
     * counted here.
     */
    class ActionValidator
    {
      public:
        explicit ActionValidator(bool enabled = true) : m_enabled(enabled) {}
        void setEnabled(bool e) { m_enabled = e; }
        bool enabled() const { return m_enabled; }

        // laneCount/laneIndex are the vehicle's current segment lane count and lane,
        // used only for laneChange target-lane feasibility.
        uint32_t validate(Action& a, int laneCount, uint8_t laneIndex, const IdmParams& p) const;

      private:
        bool m_enabled{true};
    };

} // namespace tfv

#endif // TFV_ACTION_VALIDATOR_HPP
