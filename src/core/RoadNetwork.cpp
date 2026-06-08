#include "core/RoadNetwork.hpp"
#include "core/Configuration.hpp"
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
        m_movements.clear();     // ... or stale movement tables

        std::ifstream file(path);
        if(!file.is_open())
        {
            LOG_ERROR("[Road] could not open {file}", PARAM(file, path.string()));
            return false;
        }

        uint32_t nextNodeId = 1; // Start node IDs at 1
        std::unordered_map<std::pair<int, int>, uint32_t, pair_hash>
            nodeMap; // Map positions to node IDs
        auto makeCoordPair = [](int x, int y) -> std::pair<int, int> { return {x, y}; };

        // Build all entities for one road row. SHARED by the v1 (positional) and v2
        // (named-column) parsers, so the two formats are byte-identical for the same data.
        auto addRoadRow = [&](uint32_t segId, int rx1, int ry1, int rx2, int ry2, int lanes) {
            RoadVisual r{};
            r.id = segId;
            r.x1 = rx1;
            r.y1 = ry1;
            r.x2 = rx2;
            r.y2 = ry2;
            float dx = static_cast<float>(r.x2 - r.x1);
            float dy = static_cast<float>(r.y2 - r.y1);
            r.length = std::sqrtf(dx * dx + dy * dy);
            m_seg.emplace_back(r);

            auto fromPos = makeCoordPair(r.x1, r.y1);
            auto toPos = makeCoordPair(r.x2, r.y2);
            if(nodeMap.find(fromPos) == nodeMap.end())
            {
                nodeMap[fromPos] = nextNodeId++;
                Node node;
                node.id = nodeMap[fromPos];
                node.pos = {static_cast<float>(r.x1), static_cast<float>(r.y1)};
                m_nodes[node.id] = node;
            }
            if(nodeMap.find(toPos) == nodeMap.end())
            {
                nodeMap[toPos] = nextNodeId++;
                Node node;
                node.id = nodeMap[toPos];
                node.pos = {static_cast<float>(r.x2), static_cast<float>(r.y2)};
                m_nodes[node.id] = node;
            }
            RoadSegment segment;
            segment.id = segId;
            segment.fromNode = nodeMap[fromPos];
            segment.toNode = nodeMap[toPos];
            segment.length = r.length;
            segment.dir = glm::normalize(glm::vec2(r.x2 - r.x1, r.y2 - r.y1));
            segment.lanes = (lanes >= 1) ? lanes : 1;
            m_segments[segId] = segment;
            m_nodes[segment.fromNode].outgoing.push_back(segId);
            m_nodes[segment.toNode].incoming.push_back(segId);
        };

        auto trimws = [](std::string s) {
            while(!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
                s.pop_back();
            std::size_t b = 0;
            while(b < s.size() && (s[b] == ' ' || s[b] == '\t'))
                ++b;
            return s.substr(b);
        };

        std::vector<uint32_t> twoWayIds; // forward ids flagged dir=twoway (synthesize reverses)
        std::string first;
        std::getline(file, first); // first line: a `#tfv-roads v2` directive, or the v1 header
        if(trimws(first) == "#tfv-roads v2")
        {
            // v2: named-column format. The next line is the header; columns are located by
            // NAME, so later phases can add columns (dir/turn/curve...) in any order.
            std::string header;
            std::getline(file, header);
            std::vector<std::string> cols;
            {
                std::stringstream hs(header);
                std::string c;
                while(std::getline(hs, c, ','))
                    cols.push_back(trimws(c));
            }
            auto colIdx = [&](const std::string& name) {
                for(std::size_t i = 0; i < cols.size(); ++i)
                    if(cols[i] == name)
                        return static_cast<int>(i);
                return -1;
            };
            const int ci_id = colIdx("id"), ci_x1 = colIdx("x1"), ci_y1 = colIdx("y1"),
                      ci_x2 = colIdx("x2"), ci_y2 = colIdx("y2"), ci_lanes = colIdx("lanes"),
                      ci_dir = colIdx("dir");
            if(ci_id < 0 || ci_x1 < 0 || ci_y1 < 0 || ci_x2 < 0 || ci_y2 < 0)
                LOG_ERROR("[Road] v2 header missing a required column (id,x1,y1,x2,y2)");
            std::string line;
            while((ci_id >= 0 && ci_x1 >= 0 && ci_y1 >= 0 && ci_x2 >= 0 && ci_y2 >= 0) &&
                  std::getline(file, line))
            {
                if(trimws(line).empty())
                    continue;
                std::vector<std::string> f;
                {
                    std::stringstream rs(line);
                    std::string c;
                    while(std::getline(rs, c, ','))
                        f.push_back(c);
                }
                auto cell = [&](int idx) -> std::string {
                    return (idx >= 0 && idx < static_cast<int>(f.size()))
                               ? f[static_cast<std::size_t>(idx)]
                               : std::string();
                };
                try
                {
                    const uint32_t segId = static_cast<uint32_t>(std::stoul(cell(ci_id)));
                    const int rx1 = static_cast<int>(std::stof(cell(ci_x1)));
                    const int ry1 = static_cast<int>(std::stof(cell(ci_y1)));
                    const int rx2 = static_cast<int>(std::stof(cell(ci_x2)));
                    const int ry2 = static_cast<int>(std::stof(cell(ci_y2)));
                    int lanes = 1;
                    if(ci_lanes >= 0 && !cell(ci_lanes).empty())
                        lanes = std::stoi(cell(ci_lanes));
                    addRoadRow(segId, rx1, ry1, rx2, ry2, lanes);
                    if(ci_dir >= 0 && trimws(cell(ci_dir)) == "twoway")
                        twoWayIds.push_back(segId);
                }
                catch(...)
                {
                    continue; // skip a malformed row
                }
            }
        }
        else
        {
            // v1: positional id,x1,y1,x2,y2[,lanes]. `first` was the header (skipped).
            // Coordinates are floats (e.g. 541.42) truncated to the int pixel fields —
            // reading a float straight into an int via >> stops at the '.'.
            std::string line;
            while(std::getline(file, line))
            {
                std::stringstream ss(line);
                char comma;
                uint32_t segId;
                float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                ss >> segId >> comma >> x1 >> comma >> y1 >> comma >> x2 >> comma >> y2;
                if(!ss)
                    continue;
                int lanes = 1;
                {
                    char c2;
                    int L;
                    if(ss >> c2 >> L && L >= 1)
                        lanes = L;
                }
                addRoadRow(segId, static_cast<int>(x1), static_cast<int>(y1),
                           static_cast<int>(x2), static_cast<int>(y2), lanes);
            }
        }
        // Two-way roads (Phase B): synthesize each flagged road's opposing reverse segment.
        if(!twoWayIds.empty())
        {
            const float laneW = TFV_CONFIG().getFloat("sim.lane_width_m", 3.5f);
            const float medW = TFV_CONFIG().getFloat("sim.median_width_m", 3.5f);
            for(uint32_t fid : twoWayIds)
                makeTwoWay(fid, laneW, medW);
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

    uint32_t RoadNetwork::makeTwoWay(uint32_t forwardId, float laneWidth, float medianWidth)
    {
        auto it = m_segments.find(forwardId);
        if(it == m_segments.end())
            return 0;
        if(it->second.pairId != 0)
            return it->second.pairId; // already two-way
        // Snapshot the forward values before any insert (which can rehash the map).
        const uint32_t fFrom = it->second.fromNode, fTo = it->second.toNode;
        const float fLen = it->second.length;
        const int fLanes = std::max(1, it->second.lanes);
        const float fSpeed = it->second.speedLimit;
        const glm::vec2 fDir = it->second.dir;
        const float halfMedian =
            0.5f * static_cast<float>(fLanes) * laneWidth + 0.5f * medianWidth;

        // Fresh reverse id above all current ids (deterministic, collision-checked).
        uint32_t maxId = 0;
        for(const auto& [id, s] : m_segments)
        {
            (void)s;
            if(id > maxId)
                maxId = id;
        }
        if(maxId == UINT32_MAX) // pathological CSV: no headroom for a synthetic id
        {
            LOG_ERROR("[Road] cannot make segment {id} two-way: id space exhausted",
                      PARAM(id, forwardId));
            return 0;
        }
        uint32_t revId = maxId + 1;
        while(m_segments.count(revId))
            ++revId;

        // Mark the forward direction (writes land before the insert below).
        it->second.oneway = false;
        it->second.medianOffset = halfMedian;
        it->second.pairId = revId;

        // Synthesize the opposing reverse directed segment.
        RoadSegment rev;
        rev.id = revId;
        rev.fromNode = fTo;
        rev.toNode = fFrom;
        rev.length = fLen;
        rev.lanes = fLanes;
        rev.speedLimit = fSpeed;
        rev.dir = -fDir;
        rev.oneway = false;
        // Same magnitude/sign as the forward: the reverse's normal is already flipped
        // (its dir is negated), so +halfMedian along its own normal lands on the OPPOSITE
        // side of the road from the forward direction.
        rev.medianOffset = halfMedian;
        rev.pairId = forwardId;
        m_segments[revId] = rev; // may rehash m_segments; do not touch `it` after this
        m_nodes[rev.fromNode].outgoing.push_back(revId);
        m_nodes[rev.toNode].incoming.push_back(revId);

        // Reverse visual (swapped endpoints) + tag the forward visual for the renderer.
        RoadVisual rv{};
        rv.id = revId;
        if(const Node* a = getNode(rev.fromNode))
        {
            rv.x1 = static_cast<int>(a->pos.x);
            rv.y1 = static_cast<int>(a->pos.y);
        }
        if(const Node* b = getNode(rev.toNode))
        {
            rv.x2 = static_cast<int>(b->pos.x);
            rv.y2 = static_cast<int>(b->pos.y);
        }
        rv.length = fLen;
        rv.medianOffset = halfMedian; // reverse visual's normal is flipped -> opposite side
        rv.pairId = forwardId;
        m_seg.push_back(rv);
        for(auto& vis : m_seg)
            if(vis.id == forwardId)
            {
                vis.medianOffset = halfMedian;
                vis.pairId = revId;
                break; // exactly one forward visual
            }
        return revId;
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

    void RoadNetwork::addMovement(uint32_t nodeId, const Movement& m)
    {
        m_movements[nodeId].push_back(m);
    }

    bool RoadNetwork::movementAllowed(uint32_t inSeg, uint32_t outSeg) const
    {
        const RoadSegment* s = getSegment(inSeg);
        if(!s)
            return true;
        auto it = m_movements.find(s->toNode);
        if(it == m_movements.end())
            return true; // no table for this node => every movement permitted (today)
        for(const auto& m : it->second)
            if(m.inSeg == inSeg && m.outSeg == outSeg)
                return true;
        return false;
    }

    TurnType RoadNetwork::turnTypeFor(uint32_t inSeg, uint32_t outSeg) const
    {
        const RoadSegment* a = getSegment(inSeg);
        const RoadSegment* b = getSegment(outSeg);
        if(!a || !b)
            return TurnType::STRAIGHT;
        const glm::vec2 i = a->dir, o = b->dir;
        const float cross = i.x * o.y - i.y * o.x;
        const float dot = i.x * o.x + i.y * o.y;
        const float eps = 0.342f; // sin(20deg): a clean 90deg turn is unambiguously L/R
        if(dot < -0.7f)
            return TurnType::U;
        if(cross > eps)
            return TurnType::LEFT;
        if(cross < -eps)
            return TurnType::RIGHT;
        return TurnType::STRAIGHT;
    }

    bool RoadNetwork::loadMovementsCSV(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if(!file.is_open())
            return false; // movements are optional: missing file is a no-op
        auto parseTurn = [](std::string s) -> TurnType {
            for(auto& c : s)
                c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
            if(s == "LEFT")
                return TurnType::LEFT;
            if(s == "RIGHT")
                return TurnType::RIGHT;
            if(s == "U" || s == "UTURN")
                return TurnType::U;
            return TurnType::STRAIGHT;
        };
        std::string line;
        std::getline(file, line); // header
        int loaded = 0;
        while(std::getline(file, line))
        {
            if(line.empty())
                continue;
            std::stringstream ss(line);
            std::string nodeS, inS, outS, turnS;
            if(!std::getline(ss, nodeS, ','))
                continue;
            std::getline(ss, inS, ',');
            std::getline(ss, outS, ',');
            std::getline(ss, turnS, ',');
            try
            {
                const uint32_t nodeId = static_cast<uint32_t>(std::stoul(nodeS));
                const uint32_t inSeg = static_cast<uint32_t>(std::stoul(inS));
                const uint32_t outSeg = static_cast<uint32_t>(std::stoul(outS));
                if(!getNode(nodeId) || !getSegment(inSeg) || !getSegment(outSeg))
                    continue; // skip dangling rows (never insert an unresolved movement)
                Movement m;
                m.inSeg = inSeg;
                m.outSeg = outSeg;
                m.turn = turnS.empty() ? turnTypeFor(inSeg, outSeg) : parseTurn(turnS);
                addMovement(nodeId, m);
                ++loaded;
            }
            catch(const std::exception&)
            {
                // skip malformed row
            }
        }
        LOG_INFO("loaded {count} movements from {file}", PARAM(count, loaded),
                 PARAM(file, path.string()));
        return true;
    }

    void RoadNetwork::setLaneTurns(uint32_t segId, uint8_t laneIndex, uint8_t allowedTurns)
    {
        RoadSegment* s = getSegment(segId);
        if(!s)
            return;
        const int n = std::max(1, s->lanes);
        if(static_cast<int>(laneIndex) >= n)
            return; // out-of-range lane: ignore (never grow past the segment's lane count)
        if(static_cast<int>(s->laneDefs.size()) < n)
        {
            // Materialize the implicit all-permissive lanes before restricting one of them,
            // so an unauthored lane keeps its 0x0F default.
            const std::size_t old = s->laneDefs.size();
            s->laneDefs.resize(static_cast<std::size_t>(n));
            for(std::size_t i = old; i < s->laneDefs.size(); ++i)
            {
                s->laneDefs[i].index = static_cast<uint8_t>(i);
                s->laneDefs[i].allowedTurns = 0x0F;
            }
        }
        s->laneDefs[laneIndex].allowedTurns = allowedTurns;
    }

    bool RoadNetwork::loadLanesCSV(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if(!file.is_open())
            return false; // lane turns are optional: missing file is a no-op
        // Parse a turns cell as either a bitmask integer (e.g. "5") or a string of
        // S/L/R/U characters (e.g. "SR", "S|L"). Empty / unrecognized => all-permissive.
        auto parseTurns = [](std::string s) -> uint8_t {
            std::size_t b = 0;
            while(b < s.size() && (s[b] == ' ' || s[b] == '\t'))
                ++b;
            s = s.substr(b);
            while(!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
                s.pop_back();
            if(s.empty())
                return 0x0F;
            bool allDigits = true;
            for(char c : s)
                if(!std::isdigit(static_cast<unsigned char>(c)))
                {
                    allDigits = false;
                    break;
                }
            if(allDigits)
            {
                try
                {
                    return static_cast<uint8_t>(std::stoul(s) & 0x0F);
                }
                catch(...)
                {
                    return 0x0F;
                }
            }
            uint8_t mask = 0;
            for(char c : s)
            {
                switch(::toupper(static_cast<unsigned char>(c)))
                {
                case 'S':
                    mask |= turnBit(TurnType::STRAIGHT);
                    break;
                case 'L':
                    mask |= turnBit(TurnType::LEFT);
                    break;
                case 'R':
                    mask |= turnBit(TurnType::RIGHT);
                    break;
                case 'U':
                    mask |= turnBit(TurnType::U);
                    break;
                default:
                    break; // skip separators ('|', '+', spaces, ...)
                }
            }
            return mask ? mask : 0x0F;
        };
        std::string line;
        std::getline(file, line); // header
        int loaded = 0;
        while(std::getline(file, line))
        {
            if(line.empty())
                continue;
            std::stringstream ss(line);
            std::string segS, laneS, turnsS;
            if(!std::getline(ss, segS, ','))
                continue;
            std::getline(ss, laneS, ',');
            std::getline(ss, turnsS, ',');
            try
            {
                const uint32_t segId = static_cast<uint32_t>(std::stoul(segS));
                const long laneIdx = std::stol(laneS); // long: an out-of-range index can't wrap
                const RoadSegment* s = getSegment(segId);
                if(!s)
                    continue; // dangling segment id
                // Reject (don't silently wrap into a valid lane via the uint8_t cast, and don't
                // count) any lane index outside the segment's real lane range.
                if(laneIdx < 0 || laneIdx >= std::max(1, s->lanes))
                {
                    LOG_ERROR("[Road] lanes.csv: lane {lane} out of range for segment {seg}",
                              PARAM(lane, laneIdx), PARAM(seg, segId));
                    continue;
                }
                setLaneTurns(segId, static_cast<uint8_t>(laneIdx), parseTurns(turnsS));
                ++loaded;
            }
            catch(const std::exception&)
            {
                // skip malformed row
            }
        }
        LOG_INFO("loaded {count} lane-turn rows from {file}", PARAM(count, loaded),
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