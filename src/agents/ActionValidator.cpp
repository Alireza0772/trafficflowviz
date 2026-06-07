#include "agents/ActionValidator.hpp"

#include <algorithm>
#include <cmath>

namespace tfv
{
    uint32_t ActionValidator::validate(Action& a, int laneCount, uint8_t laneIndex,
                                       const IdmParams& p) const
    {
        if(!m_enabled)
            return 0;
        uint32_t violations = 0;

        // Scrub non-finite outputs (a misbehaving net could emit NaN/Inf).
        if(!std::isfinite(a.accel))
        {
            a.accel = 0.0f;
            ++violations;
        }
        if(!std::isfinite(a.reserved0))
            a.reserved0 = 0.0f;

        // Acceleration within physical limits (identity on already-clamped IDM output).
        const float clamped = std::clamp(a.accel, -p.b_max, p.a_max);
        if(clamped != a.accel)
        {
            a.accel = clamped;
            ++violations;
        }

        // laneChange in {-1,0,+1} and the target lane must exist on this segment.
        if(a.laneChange < -1 || a.laneChange > 1)
        {
            a.laneChange = 0;
            ++violations;
        }
        if(a.laneChange != 0)
        {
            const int target = static_cast<int>(laneIndex) + a.laneChange;
            if(target < 0 || target >= std::max(1, laneCount))
            {
                a.laneChange = 0;
                ++violations;
            }
        }

        // turn in {-1,0,1,2}.
        if(a.turn < -1 || a.turn > 2)
        {
            a.turn = 0;
            ++violations;
        }

        // lightCmd: only the defined light:: bits (BRAKE|LEFT|RIGHT|HAZARD|FLASH = 0x1F).
        const uint8_t masked = static_cast<uint8_t>(a.lightCmd & 0x1F);
        if(masked != a.lightCmd)
        {
            a.lightCmd = masked;
            ++violations;
        }

        return violations;
    }

} // namespace tfv
