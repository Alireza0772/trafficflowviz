#include "core/RoadNetwork.hpp"
#include "utils/LoggingManager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
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
        m_segments.clear();
        m_nodes.clear();
        m_signs.clear();         // a bare reload leaves no stale signs ...
        m_intersections.clear(); // ... or dangling intersections

        std::ifstream file(path);
        if(!file.is_open())
        {
            LOG_ERROR("[Road] could not open {file}", PARAM(file, path.string()));
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

            // Coordinates are floats in the CSV (e.g. 541.42). Parse as float and
            // truncate to the int pixel fields — reading floats straight into int
            // via >> stops at the '.', corrupting every later coordinate.
            uint32_t segId;
            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            ss >> segId >> comma >> x1 >> comma >> y1 >> comma >> x2 >> comma >> y2;
            r.id = segId;
            r.x1 = static_cast<int>(x1);
            r.y1 = static_cast<int>(y1);
            r.x2 = static_cast<int>(x2);
            r.y2 = static_cast<int>(y2);

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

                // Optional trailing 'lanes' column (default 1; keeps 5-column CSVs valid).
                int lanes = 1;
                {
                    char c2;
                    int L;
                    if(ss >> c2 >> L && L >= 1)
                        lanes = L;
                }
                segment.lanes = lanes;

                // Add to segment map
                m_segments[segId] = segment;

                // Update node adjacency (single source of truth for routing + signals)
                m_nodes[segment.fromNode].outgoing.push_back(segId);
                m_nodes[segment.toNode].incoming.push_back(segId);
            }
        }
        LOG_INFO("loaded {count} segments from {file}", PARAM(count, m_seg.size()),
                 PARAM(file, path.string()));
        LOG_INFO("created {count} nodes", PARAM(count, m_nodes.size()));
        return !m_seg.empty();
    }

    std::vector<uint32_t> RoadNetwork::route(uint32_t srcNode, uint32_t dstNode) const
    {
        if(srcNode == dstNode)
            return {};

        // Deterministic BFS over the NODE graph using Node.outgoing (segment ids).
        // outgoing is in stable insertion order, so tie-breaks are reproducible.
        std::unordered_map<uint32_t, uint32_t> prevSeg; // nextNode -> segment used to reach it
        std::queue<uint32_t> q;
        q.push(srcNode);
        std::unordered_set<uint32_t> visited{srcNode};

        while(!q.empty())
        {
            const uint32_t n = q.front();
            q.pop();
            const Node* node = getNode(n);
            if(!node)
                continue;
            for(uint32_t segId : node->outgoing)
            {
                const RoadSegment* seg = getSegment(segId);
                if(!seg)
                    continue;
                const uint32_t nextNode = seg->toNode;
                if(!visited.insert(nextNode).second)
                    continue;
                prevSeg[nextNode] = segId;
                if(nextNode == dstNode)
                {
                    // Back-track segment ids from dst to src.
                    std::vector<uint32_t> route;
                    uint32_t cur = dstNode;
                    while(cur != srcNode)
                    {
                        auto it = prevSeg.find(cur);
                        if(it == prevSeg.end())
                            return {}; // defensive: broken chain
                        const uint32_t id = it->second;
                        route.push_back(id);
                        const RoadSegment* s = getSegment(id);
                        if(!s)
                            return {};
                        cur = s->fromNode;
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

        // Update the destination node's incoming list (signals/intersections).
        if(auto* toNode = getNode(segment.toNode))
            toNode->incoming.push_back(segment.id);

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
        }
    }

    void RoadNetwork::addNode(const Node& node)
    {
        m_nodes[node.id] = node;
    }

    void RoadNetwork::addSign(const Sign& sign)
    {
        m_signs[sign.id] = sign;
        if(auto* seg = getSegment(sign.segmentId))
        {
            // Idempotent: don't list the same sign id twice on re-load.
            if(std::find(seg->signIds.begin(), seg->signIds.end(), sign.id) == seg->signIds.end())
                seg->signIds.push_back(sign.id);
        }
    }

    const Sign* RoadNetwork::getSign(uint32_t id) const
    {
        auto it = m_signs.find(id);
        return (it != m_signs.end()) ? &it->second : nullptr;
    }

    void RoadNetwork::buildIntersections(int minApproaches)
    {
        m_intersections.clear();
        for(const auto& [nodeId, node] : m_nodes)
        {
            if(static_cast<int>(node.incoming.size()) < minApproaches)
                continue;
            Intersection x;
            x.nodeId = nodeId;
            x.approaches = node.incoming;
            std::sort(x.approaches.begin(), x.approaches.end()); // deterministic phase order
            x.approaches.erase(std::unique(x.approaches.begin(), x.approaches.end()),
                               x.approaches.end()); // robust to duplicate approaches
            m_intersections[nodeId] = std::move(x);
        }
        LOG_INFO("built {count} signalized intersections", PARAM(count, m_intersections.size()));
    }

    LightColor RoadNetwork::approachColor(uint32_t segmentId) const
    {
        const RoadSegment* seg = getSegment(segmentId);
        if(!seg)
            return LightColor::Green;
        auto it = m_intersections.find(seg->toNode);
        if(it == m_intersections.end())
            return LightColor::Green; // unsignalized node
        const Intersection& x = it->second;
        if(x.currentPhase < x.approaches.size() && x.approaches[x.currentPhase] == segmentId)
            return x.activeColor;     // the active approach (green/amber/all-red)
        return LightColor::Red;       // a non-active approach is always red
    }

    bool RoadNetwork::loadSignsCSV(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if(!file.is_open())
            return false; // signs are optional: missing file is a no-op

        auto parseType = [](std::string s) -> SignType {
            for(auto& c : s)
                c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
            if(s == "STOP")
                return SignType::STOP;
            if(s == "YIELD")
                return SignType::YIELD;
            if(s == "SPEED_LIMIT" || s == "SPEEDLIMIT" || s == "LIMIT")
                return SignType::SPEED_LIMIT;
            return SignType::NONE;
        };

        std::string line;
        std::getline(file, line); // header
        int loaded = 0;
        while(std::getline(file, line))
        {
            if(line.empty())
                continue;
            std::stringstream ss(line);
            std::string idS, segS, typeS, valS, posS, maskS;
            if(!std::getline(ss, idS, ','))
                continue;
            std::getline(ss, segS, ',');
            std::getline(ss, typeS, ',');
            std::getline(ss, valS, ',');
            std::getline(ss, posS, ',');
            std::getline(ss, maskS, ',');
            try
            {
                Sign s;
                s.id = static_cast<uint32_t>(std::stoul(idS));
                s.segmentId = static_cast<uint32_t>(std::stoul(segS));
                s.type = parseType(typeS);
                s.value = valS.empty() ? 0.0f : std::stof(valS);
                s.pos = posS.empty() ? 1.0f : std::stof(posS);
                if(!maskS.empty())
                    s.laneMask = static_cast<uint32_t>(std::stoul(maskS));
                addSign(s);
                ++loaded;
            }
            catch(const std::exception&)
            {
                // skip malformed row
            }
        }
        LOG_INFO("loaded {count} signs from {file}", PARAM(count, loaded),
                 PARAM(file, path.string()));
        return true;
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