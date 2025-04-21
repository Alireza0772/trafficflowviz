#include "core/RoadNetwork.hpp"

#include <cmath>
#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace tfv
{
    // Hash function for pairs - define this before it's used
    struct pair_hash
    {
        size_t operator()(const std::pair<int, int>& p) const
        {
            return std::hash<int>()(p.first) ^ std::hash<int>()(p.second);
        }
    };

    RoadNetwork::RoadNetwork() = default;

    bool RoadNetwork::loadCSV(const std::filesystem::path& path)
    {
        m_seg.clear();
        m_adj.clear(); // Initialize adjacency list
        m_segments.clear();
        m_nodes.clear();

        std::ifstream file(path);
        if(!file.is_open())
        {
            std::cerr << "[Road] could not open " << path << '\n';
            return false;
        }

        std::string line;
        std::getline(file, line); // skip header

        uint32_t nextNodeId = 1; // Start node IDs at 1
        std::unordered_map<std::pair<int, int>, uint32_t, pair_hash>
            nodeMap; // Map positions to node IDs

        // Helper to create a hash for a pair
        auto makeCoordPair = [](int x, int y) -> std::pair<int, int> { return {x, y}; };

        while(std::getline(file, line))
        {
            std::stringstream ss(line);
            RoadVisual r{};
            char comma;

            uint32_t segId;
            ss >> segId >> comma >> r.x1 >> comma >> r.y1 >> comma >> r.x2 >> comma >> r.y2;
            r.id = segId;

            if(ss)
            {
                // compute pixel length once
                float dx = static_cast<float>(r.x2 - r.x1);
                float dy = static_cast<float>(r.y2 - r.y1);
                r.length = std::sqrtf(dx * dx + dy * dy);
                m_seg.emplace_back(r);

                // Create nodes if they don't exist yet
                auto fromPos = makeCoordPair(r.x1, r.y1);
                auto toPos = makeCoordPair(r.x2, r.y2);

                if(nodeMap.find(fromPos) == nodeMap.end())
                {
                    nodeMap[fromPos] = nextNodeId++;

                    // Create the node entity
                    Node node;
                    node.id = nodeMap[fromPos];
                    node.pos = {static_cast<float>(r.x1), static_cast<float>(r.y1)};
                    m_nodes[node.id] = node;
                }

                if(nodeMap.find(toPos) == nodeMap.end())
                {
                    nodeMap[toPos] = nextNodeId++;

                    // Create the node entity
                    Node node;
                    node.id = nodeMap[toPos];
                    node.pos = {static_cast<float>(r.x2), static_cast<float>(r.y2)};
                    m_nodes[node.id] = node;
                }

                // Create the road segment entity
                RoadSegment segment;
                segment.id = segId;
                segment.fromNode = nodeMap[fromPos];
                segment.toNode = nodeMap[toPos];
                segment.length = r.length;

                // Calculate direction vector
                segment.dir = glm::normalize(glm::vec2(r.x2 - r.x1, r.y2 - r.y1));

                // Add to segment map
                m_segments[segId] = segment;

                // Update node's outgoing segments
                m_nodes[segment.fromNode].outgoing.push_back(segId);

                // Update adjacency list for routing
                m_adj[r.x1].push_back(r.id);
                m_adj[r.x2].push_back(r.id);
            }
        }

        std::cout << "[Road] loaded " << m_seg.size() << " segments from " << path << '\n';
        std::cout << "[Road] created " << m_nodes.size() << " nodes\n";
        return !m_seg.empty();
    }

    std::vector<uint32_t> RoadNetwork::route(uint32_t src, uint32_t dst) const
    {
        if(src == dst)
            return {};
        std::unordered_map<uint32_t, uint32_t> prevSeg;
        std::queue<uint32_t> q;
        q.push(src);
        std::unordered_set<uint32_t> visited{src};
        while(!q.empty())
        {
            uint32_t n = q.front();
            q.pop();
            for(uint32_t segId : m_adj.at(n))
            {
                const auto& seg = m_seg[segId];
                uint32_t nextNode = (seg.x1 == n) ? seg.x2 : seg.x1;
                if(!visited.insert(nextNode).second)
                    continue;
                prevSeg[nextNode] = segId;
                if(nextNode == dst)
                { // back‑track
                    std::vector<uint32_t> route;
                    uint32_t cur = dst;
                    while(cur != src)
                    {
                        uint32_t id = prevSeg[cur];
                        route.push_back(id);
                        const auto& s = m_seg[id];
                        cur = (s.x1 == cur) ? s.x2 : s.x1;
                    }
                    std::reverse(route.begin(), route.end());
                    return route;
                }
                q.push(nextNode);
            }
        }
        return {}; // no path
    }

    RoadSegment* RoadNetwork::getSegment(uint32_t segmentId)
    {
        auto it = m_segments.find(segmentId);
        if(it != m_segments.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    const RoadSegment* RoadNetwork::getSegment(uint32_t segmentId) const
    {
        auto it = m_segments.find(segmentId);
        if(it != m_segments.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    Node* RoadNetwork::getNode(uint32_t nodeId)
    {
        auto it = m_nodes.find(nodeId);
        if(it != m_nodes.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    const Node* RoadNetwork::getNode(uint32_t nodeId) const
    {
        auto it = m_nodes.find(nodeId);
        if(it != m_nodes.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    std::vector<uint32_t> RoadNetwork::getSegmentIds() const
    {
        std::vector<uint32_t> ids;
        ids.reserve(m_segments.size());
        for(const auto& [id, _] : m_segments)
        {
            ids.push_back(id);
        }
        return ids;
    }

    void RoadNetwork::addSegment(const RoadSegment& segment)
    {
        m_segments[segment.id] = segment;

        // Update the node's outgoing list
        auto* fromNode = getNode(segment.fromNode);
        if(fromNode)
        {
            fromNode->outgoing.push_back(segment.id);
        }

        // Create visual segment
        RoadVisual vis;
        vis.id = segment.id;

        // Get node positions
        const auto* fromNodeConst = getNode(segment.fromNode);
        const auto* toNodeConst = getNode(segment.toNode);

        if(fromNodeConst && toNodeConst)
        {
            vis.x1 = static_cast<int>(fromNodeConst->pos.x);
            vis.y1 = static_cast<int>(fromNodeConst->pos.y);
            vis.x2 = static_cast<int>(toNodeConst->pos.x);
            vis.y2 = static_cast<int>(toNodeConst->pos.y);
            vis.length = segment.length;

            m_seg.push_back(vis);

            // Update adjacency list for routing
            m_adj[vis.x1].push_back(vis.id);
            m_adj[vis.x2].push_back(vis.id);
        }
    }

    void RoadNetwork::addNode(const Node& node)
    {
        m_nodes[node.id] = node;
    }

} // namespace tfv

// Hash function for pair
namespace std
{
    template <> struct hash<std::pair<int, int>>
    {
        size_t operator()(const std::pair<int, int>& p) const
        {
            return hash<int>()(p.first) ^ hash<int>()(p.second);
        }
    };
} // namespace std