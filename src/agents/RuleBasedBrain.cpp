#include "agents/RuleBasedBrain.hpp"

#include <algorithm>
#include <cmath>

namespace tfv
{
    void RuleBasedBrain::decideBatch(const Observation* obs, int n, Action* out)
    {
        for(int i = 0; i < n; ++i)
        {
            const Observation& o = obs[i];

            // Reconstruct absolute quantities from the normalized observation.
            const float v = o[obs_idx::SelfSpeed] * OBS_SPEED_SCALE;
            float v0 = std::min(o[obs_idx::SpeedLimit] * OBS_SPEED_SCALE, m_p.v0_cap);
            v0 = std::max(v0, 0.1f); // avoid div-by-zero in the free term

            // Free-road term: accelerate toward the desired speed.
            const float aFree = m_p.a_max * (1.0f - std::pow(v / v0, m_p.delta));

            float a = aFree;
            if(o[obs_idx::FrontHasLeader] > 0.5f)
            {
                // Interaction term: brake for the leader (IDM desired minimum gap s*).
                const float gap = std::max(o[obs_idx::FrontGap] * OBS_RANGE_SCALE, 0.1f);
                const float dv = o[obs_idx::FrontRelSpeed] * OBS_SPEED_SCALE; // v - v_leader
                const float sStar =
                    m_p.s0 +
                    std::max(0.0f, v * m_p.T + (v * dv) / (2.0f * std::sqrt(m_p.a_max * m_p.b_comfort)));
                const float ratio = sStar / gap;
                a = aFree - m_p.a_max * ratio * ratio;
            }

            // Physical clamp (also re-applied defensively by the integrator).
            a = std::clamp(a, -m_p.b_max, m_p.a_max);

            Action act{};
            act.accel = a;
            act.laneChange = 0; // Phase 3
            act.turn = 0;       // Phase 3
            act.lightCmd = (a < -0.5f) ? light::BRAKE : 0; // brake light when decelerating
            out[i] = act;
        }
    }

} // namespace tfv
