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
        float length; // pre‑computed pixel length
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

        /** Add a new node to the network */
        void addNode(const Node& node);

      private:
        std::vector<RoadVisual> m_seg;
        std::unordered_map<uint32_t, std::vector<uint32_t>> m_adj; // adjacency list

        // Entities in the network
        std::unordered_map<uint32_t, RoadSegment> m_segments;
        std::unordered_map<uint32_t, Node> m_nodes;
    };

} // namespace tfv
#endif