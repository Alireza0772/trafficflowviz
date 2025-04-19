#ifndef TFV_TRAFFIC_ENTITY_HPP
#define TFV_TRAFFIC_ENTITY_HPP

#include <cstdint>
#include <string>
#include <unordered_map>

namespace tfv
{

    struct RoadSegment
    {
        uint32_t id;
        uint32_t from; // node‑id
        uint32_t to;   // node‑id
        float length;  // metres
    };

    struct Node
    {
        uint32_t id;
        float x; // pixel space for visual path‑finding demo
        float y;
    };

    struct Vehicle
    {
        uint64_t id;
        uint32_t segment; // road segment id
        float position;   // 0‥length (metres)
        float speed;      // m/s
        uint32_t sourceNode{0};
        uint32_t destNode{0};
    };

    using VehicleMap = std::unordered_map<uint64_t, Vehicle>;

} // namespace tfv
#endif