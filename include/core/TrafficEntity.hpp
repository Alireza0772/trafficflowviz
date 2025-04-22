#ifndef TFV_TRAFFIC_ENTITY_HPP
#define TFV_TRAFFIC_ENTITY_HPP

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

    // Note: Alert struct is defined in AlertManager.hpp to avoid duplication

    // Vehicle representation
    struct Vehicle
    {
        uint64_t id;             // Unique identifier
        uint32_t segmentId;      // Current road segment
        float position;          // Normalized position along segment (0-1)
        glm::vec2 vel;           // Velocity vector
        glm::vec2 acc;           // Acceleration vector
        float length{4.5f};      // Vehicle length in meters
        float width{1.8f};       // Vehicle width in meters
        std::string type{"car"}; // Vehicle type (car, truck, etc.)
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
    };

    // Node in the road network (intersection)
    struct Node
    {
        uint32_t id;                    // Unique identifier
        glm::vec2 pos;                  // Position (x, y)
        std::vector<uint32_t> incoming; // Incoming segment IDs
        std::vector<uint32_t> outgoing; // Outgoing segment IDs
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
