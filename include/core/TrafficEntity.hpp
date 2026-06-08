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

    // Turn category at a node (Phase C). Bit positions match Lane.allowedTurns's documented
    // order {straight,left,right,u}: STRAIGHT=0x01, LEFT=0x02, RIGHT=0x04, U=0x08.
    enum class TurnType : uint8_t
    {
        STRAIGHT = 0,
        LEFT,
        RIGHT,
        U
    };
    inline constexpr uint8_t turnBit(TurnType t)
    {
        return static_cast<uint8_t>(1u << static_cast<uint8_t>(t));
    }

    // Road class for the procedural city generator. Drives lane count, speed limit, median
    // width and render color, giving the generated network a legible hierarchy. NONE is the
    // default for hand-authored / CSV roads (unclassified). Ordered by capacity ascending so
    // a "max class wins" merge during generation is a simple integer compare.
    enum class RoadClass : uint8_t
    {
        LOCAL = 0,
        COLLECTOR,
        ARTERIAL,
        HIGHWAY,
        NONE = 255
    };

    // A permitted (inSeg -> outSeg) turn at a node. If a node has NO Movement entries, every
    // movement is permitted (the no-op-when-absent default that keeps existing nets frozen).
    struct Movement
    {
        uint32_t inSeg{0};
        uint32_t outSeg{0};
        TurnType turn{TurnType::STRAIGHT};
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
        glm::vec2 worldPos{0.0f, 0.0f}; // Authoritative world position (computed by Simulation)
        float heading{0.0f};            // Heading (radians) = atan2(segment.dir.y, .x)
        uint8_t lightBits{0};           // Observable lights (light:: flags); set from the action
        float length{4.5f};      // Vehicle length in meters
        float width{1.8f};       // Vehicle width in meters
        float maxSpeed{0.0f};    // per-vehicle desired-speed cap (m/s); 0 = use segment limit
        std::string type{"car"}; // Vehicle type (car, truck, etc.)

        // Phase 3: lane-keeping + routing-by-intent
        uint8_t laneIndex{0};            // current lane
        std::vector<uint32_t> route;     // ordered segment ids still to traverse (empty = wander)
        std::size_t routeIdx{0};         // cursor into route
        uint32_t destNode{0};            // destination node (0 = loop/wander)

        // Phase 6: lane-change render smoothing (cosmetic; never fed back into the sim)
        uint8_t prevLaneIndex{0};        // lane before the most recent change
        uint64_t laneChangeTick{0};      // tick of the most recent lane change

        // Phase C1c: turn-lane targeting (recomputed each tick by computeApproachIntent,
        // reset at hand-off). The sentinels mean "no turn-lane constraint active": a vehicle
        // only acquires non-sentinel values when it nears a node whose approach segment has
        // turn-restricted lanes (a lanes.csv authored allowedTurns != 0x0F). NEITHER field is
        // hashed by the golden digests, and both stay at their sentinels for every legacy net,
        // so the pinned trajectories cannot move.
        uint8_t intendedTurn{0xFF};       // turnBit() of the upcoming turn (0xFF = none computed)
        int16_t targetLaneOnApproach{-1}; // lane the vehicle must reach to make it (-1 = none).
                                          // int16_t spans the full uint8_t laneIndex range, so a
                                          // valid target can never truncate into the -1 sentinel.
    };

    // Road segment (edge in the road network)
    // Geometry of a road segment, abstracted behind offset/tangent/normal so curved roads
    // can be added later (Phase E) without touching the movement/heading call sites. The
    // straight implementation here is the LITERAL pre-abstraction arithmetic
    // (base + dir*arc, constant tangent/normal), so routing movement through it is
    // byte-identical and the pinned golden digests do not move.
    struct SegmentGeometry
    {
        glm::vec2 dir{1.0f, 0.0f}; // unit travel direction
        float length{0.0f};        // arc length (meters)

        // Offset from the segment origin (the fromNode position) at arc-length `s`.
        glm::vec2 offsetAt(float s) const { return dir * s; }
        glm::vec2 tangentAt(float /*s*/) const { return dir; }
        glm::vec2 normalAt(float /*s*/) const { return glm::vec2(-dir.y, dir.x); }
    };

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

        // Directionality (Phase B). The defaults (oneway, no pair, zero median) leave every
        // existing road byte-identical. A two-way road is authored as ONE segment plus an
        // auto-synthesized reverse segment (cross-linked via pairId); each direction is
        // shifted laterally by medianOffset so opposing traffic never overlaps.
        bool oneway{true};
        uint32_t pairId{0};       // opposing reverse segment id (0 = none)
        float medianOffset{0.0f}; // lateral shift of this direction's centerline (meters)
        RoadClass roadClass{RoadClass::NONE}; // procedural class (NONE = hand-authored/CSV)

        // Geometry seam (Phase A). Straight today; Phase E returns a stored curve geom.
        SegmentGeometry geometry() const { return SegmentGeometry{dir, length}; }
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
