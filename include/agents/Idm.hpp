#ifndef TFV_IDM_HPP
#define TFV_IDM_HPP

#include "agents/IdmParams.hpp"

#include <algorithm>
#include <cmath>

namespace tfv
{
    /**
     * Intelligent Driver Model longitudinal acceleration (Treiber). Extracted
     * VERBATIM from RuleBasedBrain so the rule brain and the MOBIL lane-change
     * evaluator share ONE implementation (and the golden digest is unaffected).
     *
     * gapM is the bumper-to-bumper gap to the leader in meters; dv = v - v_leader.
     * When hasLeader is false the interaction term is skipped (free-road accel).
     */
    inline float idmAccel(float v, float v0, float gapM, float dv, bool hasLeader,
                          const IdmParams& p)
    {
        const float aFree = p.a_max * (1.0f - std::pow(v / v0, p.delta));
        float a = aFree;
        if(hasLeader)
        {
            const float gap = std::max(gapM, 0.1f);
            const float sStar =
                p.s0 +
                std::max(0.0f, v * p.T + (v * dv) / (2.0f * std::sqrt(p.a_max * p.b_comfort)));
            const float ratio = sStar / gap;
            a = aFree - p.a_max * ratio * ratio;
        }
        return std::clamp(a, -p.b_max, p.a_max);
    }

} // namespace tfv

#endif // TFV_IDM_HPP
