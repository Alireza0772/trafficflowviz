#ifndef TFV_TRAFFIC_ENTITY_HPP
#define TFV_TRAFFIC_ENTITY_HPP

#include <array>
#include <chrono>
#include <cstdint>
#include <glm/vec2.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace tfv
{
    struct RoadSegment; // fwd

    struct Node
    {
        uint32_t id;
        glm::vec2 pos;                  // world units (pixels OR metres – pick one)
        std::vector<uint32_t> outgoing; // segment IDs, cheap & cycle‑free
    };

    struct RoadSegment
    {
        uint32_t id;
        uint32_t fromNode; // FK to Node::id
        uint32_t toNode;   // FK to Node::id
        int laneCount{1};
        bool oneWay{false};
        // cached geometry
        float length{0.f};
        glm::vec2 dir; // unit vector from 'from' to 'to'

        // Traffic metrics
        float freeFlowSpeed{10.0f};  // baseline speed with no congestion
        float currentSpeed{10.0f};   // current average speed on this segment
        int vehicleCount{0};         // current number of vehicles on segment
        float congestionLevel{0.0f}; // 0.0 (free flow) to 1.0 (standstill)
    };

    struct Vehicle
    {
        uint64_t id;
        uint32_t segmentId;  // current road segment
        float position{0.f}; // 0‑1 along that segment
        glm::vec2 vel{0.f};  // world units / s
        float length{4.f};
        float width{2.f};
        std::string type{"car"}; // vehicle type (car, bus, truck, etc.)
    };

    // Statistics for each road segment over time
    struct SegmentStatistics
    {
        static constexpr size_t HISTORY_SIZE = 60; // Store last 60 timepoints

        std::array<float, HISTORY_SIZE> avgSpeed{};   // Average speed over time
        std::array<int, HISTORY_SIZE> vehicleCount{}; // Vehicle count over time
        size_t currentIndex{0};                       // Current index in circular buffer

        // Timestamps (seconds since epoch)
        std::array<double, HISTORY_SIZE> timestamps{};

        // Travel time metrics
        float histAvgTravelTime{0.0f}; // Historical average travel time
        float currTravelTime{0.0f};    // Current travel time

        void addSample(float speed, int count)
        {
            using namespace std::chrono;

            // Get current time
            auto now = high_resolution_clock::now();
            auto epoch = now.time_since_epoch();
            double timestamp = duration_cast<duration<double>>(epoch).count();

            // Store in circular buffer
            avgSpeed[currentIndex] = speed;
            vehicleCount[currentIndex] = count;
            timestamps[currentIndex] = timestamp;

            // Move to next index
            currentIndex = (currentIndex + 1) % HISTORY_SIZE;
        }
    };

    using VehicleMap = std::unordered_map<uint64_t, Vehicle>;
    using SegmentStatsMap = std::unordered_map<uint32_t, SegmentStatistics>;

} // namespace tfv
#endif