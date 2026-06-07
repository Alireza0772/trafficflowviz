#ifndef TFV_PERCEPTION_SYSTEM_HPP
#define TFV_PERCEPTION_SYSTEM_HPP

#include "core/TrafficEntity.hpp"
#include "perception/SpatialIndex.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tfv
{
    struct PerceptionParams
    {
        float rangeFront{60.0f};
        float rangeRear{30.0f};
        float rangeSide{15.0f};
        float sectorFrontDeg{45.0f}; // front half-angle
        float sectorRearDeg{45.0f};  // rear half-angle
    };

    enum class Sector : int
    {
        Front = 0,
        Rear,
        Left,
        Right
    };

    struct SensedNeighbor
    {
        uint64_t id{0};
        float relDist{0.0f};   // center-to-center distance (m)
        float relSpeed{0.0f};  // closing scalar: |v_self| - |v_other|
        uint8_t lightBits{0};  // the neighbor's observable lights
        bool valid{false};
    };

    /**
     * World-space sector perception for vehicle `selfIdx`: nearest neighbor in each
     * of Front/Rear/Left/Right (ego heading frame), range-capped per sector, with
     * lower-id tie-breaks (deterministic). The Front sector is informational here —
     * car-following uses the separate PATH-relative leader, not this world cone.
     */
    std::array<SensedNeighbor, 4> sense(std::size_t selfIdx, const std::vector<Vehicle>& vehs,
                                        const UniformGrid& grid, const PerceptionParams& p);

} // namespace tfv

#endif // TFV_PERCEPTION_SYSTEM_HPP
