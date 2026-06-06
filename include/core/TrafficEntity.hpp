#ifndef TFV_TRAFFIC_ENTITY_HPP
#define TFV_TRAFFIC_ENTITY_HPP

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace tfv
{
    // Forward declarations
    struct Vehicle;
    struct RoadSegment;
    struct Node;

    // Type aliases for collections
    using VehicleMap = std::unordered_map<uint64_t, Vehicle>;
    using SegmentStatsMap = std::unordered_map<uint32_t, struct SegmentStatistics>;

    // Traffic-sign semantics (Phase 3).
    enum class SignType : uint8_t
    {
        NONE = 0,
        STOP,
        YIELD,
        SPEED_LIMIT
    };

    // A traffic sign governing a position on a segment.
    struct Sign
    {
        uint32_t id{0};
        SignType type{SignType::NONE};
        float value{0.0f};               // SPEED_LIMIT: limit (m/s); otherwise unused
        uint32_t segmentId{0};           // segment the sign governs
        float pos{1.0f};                 // normalized position along the segment (0..1)
        uint32_t laneMask{0xFFFFFFFFu};  // lanes governed (default: all)
    };

    // A lane within a segment (forward-compatible; lane-change dynamics are Phase 5).
    struct Lane
    {
        uint8_t index{0};
        uint8_t allowedTurns{0x0F}; // bitmask over {straight,left,right,u}; default all
    };

    // Traffic-light color (Phase 4).
    enum class LightColor : uint8_t
    {
        Green = 0,
        Amber,
        Red
    };

    // A signalized intersection at a node. A single central controller cycles which
    // incoming approach is "active": its color goes Green -> Amber -> Red (all-red
    // clearance) before advancing to the next approach. Non-active approaches are
    // always Red. (Phase 4)
    struct Intersection
    {
        uint32_t nodeId{0};
        std::vector<uint32_t> approaches;          // incoming segment ids (sorted ascending)
        uint32_t currentPhase{0};                  // index into approaches that is active
        LightColor activeColor{LightColor::Green}; // color shown to the active approach
        uint32_t ticksInPhase{0};                  // ticks elapsed in the current color
    };

    // Note: Alert struct is defined in AlertManager.hpp to avoid duplication

    // Vehicle representation
    struct Vehicle
    {
        uint64_t id{0};          // Unique identifier
        uint32_t segmentId{0};   // Current road segment
        float position{0.0f};    // Normalized position along segment (0-1)
        glm::vec2 vel{0.0f, 0.0f}; // Velocity vector
        glm::vec2 acc{0.0f, 0.0f}; // Acceleration vector (glm does not zero-init by default)
        float length{4.5f};      // Vehicle length in meters
        float width{1.8f};       // Vehicle width in meters
        std::string type{"car"}; // Vehicle type (car, truck, etc.)

        // Phase 3: lane-keeping + routing-by-intent
        uint8_t laneIndex{0};            // current lane (lane-keeping only; no lateral motion yet)
        std::vector<uint32_t> route;     // ordered segment ids still to traverse (empty = wander)
        std::size_t routeIdx{0};         // cursor into route
        uint32_t destNode{0};            // destination node (0 = loop/wander)
    };

    // Road segment (edge in the road network)
    struct RoadSegment
    {
        uint32_t id;                 // Unique identifier
        uint32_t fromNode;           // Start node
        uint32_t toNode;             // End node
        float length;                // Length in meters
        int lanes{1};                // Number of lanes
        float speedLimit{13.9f};     // Speed limit (m/s, ~50 km/h)
        int vehicleCount{0};         // Current number of vehicles
        float congestionLevel{0.0f}; // Traffic congestion level (0-1)
        float currentSpeed{13.9f};   // Current average speed (m/s)
        glm::vec2 dir;               // Direction vector (normalized)

        // Phase 3 (forward-compatible; empty => fall back to the `lanes` count)
        std::vector<Lane> laneDefs;    // explicit lane definitions (empty => use `lanes` count)
        std::vector<uint32_t> signIds; // signs governing this segment
    };

    // Node in the road network (intersection)
    struct Node
    {
        uint32_t id;                    // Unique identifier
        glm::vec2 pos;                  // Position (x, y)
        std::vector<uint32_t> incoming; // Incoming segment IDs
        std::vector<uint32_t> outgoing; // Outgoing segment IDs
        uint32_t intersectionId{0};     // owning intersection (Phase 4; unused now)
    };

    // Statistics for a road segment
    struct SegmentStatistics
    {
        float avgSpeed{0.0f};            // Average speed
        float avgDensity{0.0f};          // Average vehicle density
        std::vector<float> speedHistory; // Recent speed measurements
        std::vector<int> densityHistory; // Recent density measurements

        // Add a new sample to the statistics
        void addSample(float speed, int density)
        {
            // Update speed history (limit to last 10 samples)
            speedHistory.push_back(speed);
            if(speedHistory.size() > 10)
                speedHistory.erase(speedHistory.begin());

            // Update density history (limit to last 10 samples)
            densityHistory.push_back(density);
            if(densityHistory.size() > 10)
                densityHistory.erase(densityHistory.begin());

            // Calculate averages
            avgSpeed = 0.0f;
            for(float s : speedHistory)
                avgSpeed += s;
            avgSpeed /= speedHistory.size();

            avgDensity = 0.0f;
            for(int d : densityHistory)
                avgDensity += d;
            avgDensity /= densityHistory.size();
        }
    };

} // namespace tfv

#endif // TFV_TRAFFIC_ENTITY_HPP
