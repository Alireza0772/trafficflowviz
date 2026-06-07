#ifndef TFV_ROAD_NETWORK_HPP
#define TFV_ROAD_NETWORK_HPP

#include "core/TrafficEntity.hpp"
#include <filesystem>
#include <queue>
#include <unordered_map>
#include <vector>

namespace tfv
{
    /** One poly‑line road segment in screen space (SDL coordinates). */
    struct RoadVisual
    {
        uint32_t id; // added id as the first member
        int x1, y1;
        int x2, y2;
        float length;             // pre‑computed pixel length
        float medianOffset{0.0f}; // Phase B: lateral shift for two-way ribbon rendering
        uint32_t pairId{0};       // opposing reverse segment id (0 = one-way)
    };

    /**
     * Holds all render‑ready road segments.
     *
     * Expected CSV header (ignored) followed by:
     * id,x1,y1,x2,y2
     */
    class RoadNetwork
    {
      public:
        RoadNetwork();

        /** Clear and load from CSV; returns true on success. */
        bool loadCSV(const std::filesystem::path& path);

        const std::vector<RoadVisual>& segments() const { return m_seg; }

        /** Retrieve pixel length for a segment id (returns 0 if out of range). */
        float segmentLength(std::size_t idx) const
        {
            return (idx < m_seg.size()) ? m_seg[idx].length : 0.f;
        }

        /** Compute a simple BFS route (list of segment ids) from src node to dst
         * node. */
        std::vector<uint32_t> route(uint32_t src, uint32_t dst) const;

        /** Get a segment by ID (null if not found) */
        RoadSegment* getSegment(uint32_t segmentId);

        /** Get a segment by ID (null if not found) - const version */
        const RoadSegment* getSegment(uint32_t segmentId) const;

        /** Get a node by ID (null if not found) */
        Node* getNode(uint32_t nodeId);

        /** Get a node by ID (null if not found) - const version */
        const Node* getNode(uint32_t nodeId) const;

        /** Get all segment IDs in the network */
        std::vector<uint32_t> getSegmentIds() const;

        /** Add a new segment to the network */
        void addSegment(const RoadSegment& segment);

        /** Convert an existing one-way segment into a two-way road by synthesizing its
         *  opposing reverse segment (cross-linked via pairId, the two directions shifted
         *  apart by a median derived from laneWidth + medianWidth). The reverse id is a
         *  fresh id above all current ids. Returns the reverse id (0 on failure). */
        uint32_t makeTwoWay(uint32_t forwardId, float laneWidth, float medianWidth);

        /** Add a new node to the network */
        void addNode(const Node& node);

        /** Add a traffic sign (records it on its segment too). */
        void addSign(const Sign& sign);

        /** Optionally load signs from CSV (id,segmentId,type,value,pos[,laneMask]).
         *  Missing file is a no-op and returns false; build/run stay green. */
        bool loadSignsCSV(const std::filesystem::path& path);

        /** Get a sign by ID (null if not found). */
        const Sign* getSign(uint32_t id) const;

        /** All signs in the network. */
        const std::unordered_map<uint32_t, Sign>& signs() const { return m_signs; }

        // --- Permitted movements (Phase C). A node with NO movement entries permits every
        //     movement (the no-op-when-absent default), so existing nets stay byte-identical.
        void addMovement(uint32_t nodeId, const Movement& m);
        /** Optional sidecar (nodeId,inSeg,outSeg[,turn]); missing file is a no-op (false). */
        bool loadMovementsCSV(const std::filesystem::path& path);
        bool hasMovementTable(uint32_t nodeId) const { return m_movements.count(nodeId) != 0; }
        /** Is the (inSeg -> outSeg) turn legal? True when the turn's node has no table. */
        bool movementAllowed(uint32_t inSeg, uint32_t outSeg) const;
        /** Turn category from segment geometry (cross/dot of unit dirs). */
        TurnType turnTypeFor(uint32_t inSeg, uint32_t outSeg) const;

        /** Create signalized intersections at every node with >= minApproaches
         *  incoming segments (Phase 4). Idempotent (rebuilds from scratch). */
        void buildIntersections(int minApproaches = 2);

        /** The color a vehicle on `segmentId` faces at its toNode
         *  (Green if the toNode is not signalized). */
        LightColor approachColor(uint32_t segmentId) const;

        /** Signalized intersections (keyed by nodeId); mutable for the controller. */
        std::unordered_map<uint32_t, Intersection>& intersections() { return m_intersections; }
        const std::unordered_map<uint32_t, Intersection>& intersections() const
        {
            return m_intersections;
        }

        inline void clear()
        {
            m_seg.clear();
            m_segments.clear();
            m_nodes.clear();
            m_signs.clear();
            m_intersections.clear();
            m_movements.clear();
        }

      private:
        std::vector<RoadVisual> m_seg;

        // Entities in the network
        std::unordered_map<uint32_t, RoadSegment> m_segments;
        std::unordered_map<uint32_t, Node> m_nodes;
        std::unordered_map<uint32_t, Sign> m_signs;
        std::unordered_map<uint32_t, Intersection> m_intersections; // keyed by nodeId
        std::unordered_map<uint32_t, std::vector<Movement>> m_movements; // keyed by nodeId (Phase C)
    };

} // namespace tfv
#endif