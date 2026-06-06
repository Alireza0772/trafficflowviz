#include "perception/PerceptionSystem.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace tfv
{
    std::array<SensedNeighbor, 4> sense(std::size_t selfIdx, const std::vector<Vehicle>& vehs,
                                        const UniformGrid& grid, const PerceptionParams& p)
    {
        std::array<SensedNeighbor, 4> out{};
        std::array<float, 4> bestDist{1e30f, 1e30f, 1e30f, 1e30f};

        const Vehicle& self = vehs[selfIdx];
        const float fhx = std::cos(self.heading), fhy = std::sin(self.heading);
        const float kDeg = 3.14159265358979f / 180.0f;
        const float frontHalf = p.sectorFrontDeg * kDeg;
        const float rearHalf = p.sectorRearDeg * kDeg;
        const float kPi = 3.14159265358979f;
        const float maxR = std::max({p.rangeFront, p.rangeRear, p.rangeSide});
        const float vSelf = glm::length(self.vel);

        grid.forEachNear(self.worldPos, [&](std::size_t j) {
            if(j == selfIdx)
                return;
            const Vehicle& o = vehs[j];
            const glm::vec2 d = o.worldPos - self.worldPos;
            const float dist = glm::length(d);
            if(dist < 1e-4f || dist > maxR)
                return;

            // Signed bearing of d in the ego heading frame: 0 = dead ahead, +CCW.
            const float dot = fhx * d.x + fhy * d.y;
            const float cross = fhx * d.y - fhy * d.x;
            const float theta = std::atan2(cross, dot);
            const float a = std::fabs(theta);

            Sector sec;
            float cap;
            if(a < frontHalf)
            {
                sec = Sector::Front;
                cap = p.rangeFront;
            }
            else if(a > (kPi - rearHalf))
            {
                sec = Sector::Rear;
                cap = p.rangeRear;
            }
            else if(theta > 0.0f)
            {
                sec = Sector::Left;
                cap = p.rangeSide;
            }
            else
            {
                sec = Sector::Right;
                cap = p.rangeSide;
            }
            if(dist > cap)
                return;

            const int s = static_cast<int>(sec);
            // Keep the nearest. Candidates arrive in ascending-id order, so a plain
            // `<` keeps the lowest-id neighbor on a distance tie -> deterministic.
            if(!out[s].valid || dist < bestDist[s])
            {
                bestDist[s] = dist;
                out[s] = SensedNeighbor{o.id, dist, vSelf - glm::length(o.vel), o.lightBits, true};
            }
        });
        return out;
    }

} // namespace tfv
