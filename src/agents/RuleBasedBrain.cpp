#include "agents/RuleBasedBrain.hpp"

#include "agents/Idm.hpp"

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

            // IDM longitudinal acceleration (shared with the MOBIL evaluator).
            const bool hasLeader = o[obs_idx::FrontHasLeader] > 0.5f;
            const float gap = o[obs_idx::FrontGap] * OBS_RANGE_SCALE;     // idmAccel floors at 0.1
            const float dv = o[obs_idx::FrontRelSpeed] * OBS_SPEED_SCALE; // v - v_leader
            const float a = idmAccel(v, v0, gap, dv, hasLeader, m_p);

            Action act{};
            act.accel = a;
            act.laneChange = 0; // Phase 3
            act.turn = 0;       // Phase 3
            act.lightCmd = (a < -0.5f) ? light::BRAKE : 0; // brake light when decelerating
            out[i] = act;
        }
    }

} // namespace tfv
