#include "control/LightController.hpp"

#include "core/RoadNetwork.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tfv
{
    void LightController::reset(RoadNetwork& net)
    {
        for(auto& [nodeId, x] : net.intersections())
        {
            (void)nodeId;
            x.currentPhase = 0;
            x.activeColor = LightColor::Green;
            x.ticksInPhase = 0;
        }
    }

    void LightController::step(RoadNetwork& net, double dt)
    {
        if(dt <= 0.0)
            return;
        const uint32_t greenTicks =
            std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(m_t.greenSec / dt)));
        const uint32_t amberTicks =
            std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(m_t.amberSec / dt)));
        const uint32_t allRedTicks =
            std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(m_t.allRedSec / dt)));

        for(auto& [nodeId, x] : net.intersections())
        {
            (void)nodeId;
            if(x.approaches.empty())
                continue;
            ++x.ticksInPhase;
            switch(x.activeColor)
            {
            case LightColor::Green:
                if(x.ticksInPhase >= greenTicks)
                {
                    x.activeColor = LightColor::Amber;
                    x.ticksInPhase = 0;
                }
                break;
            case LightColor::Amber:
                if(x.ticksInPhase >= amberTicks)
                {
                    x.activeColor = LightColor::Red; // all-red clearance
                    x.ticksInPhase = 0;
                }
                break;
            case LightColor::Red:
                if(x.ticksInPhase >= allRedTicks)
                {
                    x.currentPhase =
                        (x.currentPhase + 1) % static_cast<uint32_t>(x.approaches.size());
                    x.activeColor = LightColor::Green;
                    x.ticksInPhase = 0;
                }
                break;
            }
        }
    }

} // namespace tfv
