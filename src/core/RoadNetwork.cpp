#include "core/RoadNetwork.hpp"
#include "core/Configuration.hpp"
#include "utils/LoggingManager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <glm/glm.hpp>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <set>
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
        std::sort(ids.begin(), ids.end()); // stable, deterministic order (maps iterate unordered)
        return ids;
    }

    std::vector<uint32_t> RoadNetwork::getNodeIds() const
    {
        std::vector<uint32_t> ids;
        ids.reserve(m_nodes.size());
        for(const auto& [id, _] : m_nodes)
            ids.push_back(id);
        std::sort(ids.begin(), ids.end()); // deterministic order for reproducible consumers
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
            vis.roadClass = segment.roadClass;

            m_seg.push_back(vis);
        }
    }

    void RoadNetwork::removeSegment(uint32_t id)
    {
        auto it = m_segments.find(id);
        if(it == m_segments.end())
            return;
        const uint32_t from = it->second.fromNode, to = it->second.toNode;
        if(Node* a = getNode(from))
            a->outgoing.erase(std::remove(a->outgoing.begin(), a->outgoing.end(), id),
                              a->outgoing.end());
        if(Node* b = getNode(to))
            b->incoming.erase(std::remove(b->incoming.begin(), b->incoming.end(), id),
                              b->incoming.end());
        m_segments.erase(it);
        m_seg.erase(std::remove_if(m_seg.begin(), m_seg.end(),
                                   [id](const RoadVisual& v) { return v.id == id; }),
                    m_seg.end());
    }

    void RoadNetwork::makeSomeOneWay(float fraction, uint64_t seed)
    {
        if(fraction <= 0.0f)
            return;
        // One forward per two-way road (id < pairId so each road is considered once).
        std::vector<uint32_t> cand;
        for(const auto& [id, s] : m_segments)
            if(s.pairId != 0 && !s.oneway && id < s.pairId)
                cand.push_back(id);
        std::sort(cand.begin(), cand.end());
        std::mt19937_64 rng(seed ^ 0xA5A5A5A5A5A5A5A5ULL);
        std::shuffle(cand.begin(), cand.end(), rng);
        const std::size_t target =
            static_cast<std::size_t>(fraction * static_cast<float>(cand.size()));

        // Can `src` reach `dst` over outgoing edges, ignoring segment `banned`?
        auto reaches = [&](uint32_t src, uint32_t dst, uint32_t banned) -> bool {
            std::queue<uint32_t> q;
            std::unordered_set<uint32_t> seen;
            q.push(src);
            seen.insert(src);
            while(!q.empty())
            {
                const uint32_t n = q.front();
                q.pop();
                if(n == dst)
                    return true;
                const Node* nd = getNode(n);
                if(!nd)
                    continue;
                for(uint32_t sid : nd->outgoing)
                {
                    if(sid == banned)
                        continue;
                    if(const RoadSegment* s = getSegment(sid))
                        if(seen.insert(s->toNode).second)
                            q.push(s->toNode);
                }
            }
            return false;
        };

        std::size_t made = 0;
        for(uint32_t fid : cand)
        {
            if(made >= target)
                break;
            RoadSegment* f = getSegment(fid);
            if(!f || f->oneway || f->pairId == 0)
                continue;
            const uint32_t rid = f->pairId;
            if(!getSegment(rid))
                continue;
            const uint32_t A = f->fromNode, B = f->toNode; // forward A->B, reverse rid B->A
            // Keep A->B, drop B->A — only if B can still reach A another way (no stranding).
            if(!reaches(B, A, rid))
                continue;
            removeSegment(rid);
            f = getSegment(fid); // re-fetch (erase may rehash)
            if(!f)
                continue;
            f->oneway = true;
            f->pairId = 0;
            f->medianOffset = 0.0f; // re-centre the lone carriageway on its centerline
            for(auto& vis : m_seg)
                if(vis.id == fid)
                {
                    vis.medianOffset = 0.0f;
                    vis.pairId = 0;
                    break;
                }
            ++made;
        }
        LOG_INFO("converted {n} roads to one-way (connectivity-guarded)", PARAM(n, made));
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
        const RoadClass fClass = it->second.roadClass;
        const std::vector<glm::vec2> fCenter = it->second.centerline; // empty for straight roads
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
        rev.roadClass = fClass; // the opposing carriageway shares the forward's class
        if(fCenter.size() >= 2) // curved road: the reverse follows the same curve, reversed. Its
        {                       // flipped tangent flips the normal, so +halfMedian lands opposite.
            std::vector<glm::vec2> rc(fCenter.rbegin(), fCenter.rend());
            rev.setCenterline(std::move(rc));
        }
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
        rv.roadClass = fClass;
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

    std::size_t RoadNetwork::generatePerturbedGrid(int rows, int cols, float spacing, float jitter,
                                                   float keepProb, uint64_t seed, bool twoWay,
                                                   float laneWidth, float medianWidth,
                                                   int lanesPerDir)
    {
        clear();
        if(rows < 1 || cols < 1)
            return 0;
        // Clamp jitter so two adjacent nodes can never coincide: each is shifted by up to
        // `jitter`, so an edge stays >= (spacing - 2*jitter) long. Capping at 0.45*spacing keeps
        // every segment >= 0.1*spacing, which guarantees glm::normalize(dir) is finite (no NaN).
        jitter = std::clamp(jitter, 0.0f, 0.45f * spacing);
        std::mt19937_64 rng(seed);
        std::uniform_real_distribution<float> jit(-jitter, jitter);
        std::uniform_real_distribution<float> keep(0.0f, 1.0f);
        const int lanes = std::max(1, lanesPerDir);

        // Place nodes on a grid (row-major; id = index+1), each jittered for an organic look.
        // A small margin keeps coordinates positive; SimulationLayer::fitToView() frames them.
        const float margin = spacing;
        auto nodeId = [cols](int r, int c) { return static_cast<uint32_t>(r * cols + c + 1); };
        for(int r = 0; r < rows; ++r)
            for(int c = 0; c < cols; ++c)
            {
                Node n;
                n.id = nodeId(r, c);
                n.pos = {margin + c * spacing + jit(rng), margin + r * spacing + jit(rng)};
                addNode(n);
            }

        // Candidate undirected grid edges (right + down neighbours), as 0-based index pairs.
        const int N = rows * cols;
        std::vector<std::pair<int, int>> edges;
        edges.reserve(static_cast<std::size_t>(2 * N));
        for(int r = 0; r < rows; ++r)
            for(int c = 0; c < cols; ++c)
            {
                const int idx = r * cols + c;
                if(c + 1 < cols)
                    edges.emplace_back(idx, idx + 1);     // east
                if(r + 1 < rows)
                    edges.emplace_back(idx, idx + cols);  // south
            }

        // Random spanning tree (union-find over shuffled edges) guarantees a connected graph;
        // every other edge is kept with probability keepProb. Shuffle order + the keep draws are
        // deterministic for `seed`, so the whole layout is reproducible.
        std::shuffle(edges.begin(), edges.end(), rng);
        std::vector<int> parent(static_cast<std::size_t>(N));
        std::iota(parent.begin(), parent.end(), 0);
        std::function<int(int)> find = [&](int x) {
            while(parent[static_cast<std::size_t>(x)] != x)
            {
                parent[static_cast<std::size_t>(x)] =
                    parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(x)])];
                x = parent[static_cast<std::size_t>(x)];
            }
            return x;
        };

        uint32_t segId = 0;
        std::vector<uint32_t> twoWayIds;
        for(const auto& [a, b] : edges)
        {
            const int ra = find(a), rb = find(b);
            const bool treeEdge = (ra != rb);
            if(treeEdge)
                parent[static_cast<std::size_t>(ra)] = rb; // always keep tree edges (connectivity)
            else if(keep(rng) > keepProb)
                continue; // a redundant edge we chose to drop

            const uint32_t from = static_cast<uint32_t>(a) + 1;
            const uint32_t to = static_cast<uint32_t>(b) + 1;
            const Node* na = getNode(from);
            const Node* nb = getNode(to);
            if(!na || !nb)
                continue;
            RoadSegment s{};
            s.id = segId;
            s.fromNode = from;
            s.toNode = to;
            s.dir = glm::normalize(nb->pos - na->pos);
            s.length = glm::length(nb->pos - na->pos);
            s.lanes = lanes;
            s.speedLimit = 13.9f;
            addSegment(s);
            if(twoWay)
                twoWayIds.push_back(segId);
            ++segId;
        }
        if(twoWay)
            for(uint32_t fid : twoWayIds)
                makeTwoWay(fid, laneWidth, medianWidth);

        LOG_INFO("generated {count} directed segments on a {r}x{c} perturbed grid",
                 PARAM(count, m_segments.size()), PARAM(r, rows), PARAM(c, cols));
        return m_segments.size();
    }

    namespace
    {
        // Per-class lane count / speed limit / median width. Local & collector streets are
        // undivided (median 0 -> opposing carriageways touch at a double-yellow centerline);
        // arterials and highways get a real median gap. Used by generateCity.
        struct RoadClassSpec
        {
            int lanes;
            float speed;
            float medianWidth;
        };
        RoadClassSpec classSpec(RoadClass c)
        {
            switch(c)
            {
            case RoadClass::HIGHWAY:   return {3, 30.0f, 6.0f};
            case RoadClass::ARTERIAL:  return {2, 19.0f, 2.0f};
            case RoadClass::COLLECTOR: return {1, 14.0f, 0.0f};
            case RoadClass::LOCAL:     return {1, 11.0f, 0.0f};
            default:                   return {1, 13.9f, 0.0f};
            }
        }
    } // namespace

    std::size_t RoadNetwork::generateCity(int rows, int cols, float spacing, float jitter,
                                          uint64_t seed, float laneWidth)
    {
        clear();
        if(rows < 2 || cols < 2)
            return 0;
        (void)jitter; // tensor-field roads follow the field; NO random jitter is applied

        std::mt19937_64 rng(seed);
        std::uniform_real_distribution<float> uni(0.0f, 1.0f);
        const float W = static_cast<float>(cols) * spacing;
        const float H = static_cast<float>(rows) * spacing;
        const float margin = spacing;

        // ---- Tensor field: a blend of grid + radial basis fields (Chen et al. 2008) ----
        // Each basis contributes a symmetric tensor encoded as (A,B) = w*(cos2t, sin2t). The
        // major eigenvector (the road heading) is at angle 0.5*atan2(B,A); the minor (cross
        // street) is 90 degrees off. Bases are RBF-weighted so different districts have different
        // street orientations, smoothly blended -> organic curves, and there is no jitter.
        struct Basis { int type; glm::vec2 c; float theta; float decay; };
        std::vector<Basis> bases;
        // Tunable knobs (config file / CLI / UI all drive these; see Configuration defaults).
        auto& cfg = TFV_CONFIG();
        const int nGrid = std::max(1, cfg.getInt("sim.proc.districts", 3)); // grid orientation zones
        const float invArea = 1.0f / (W * H);
        for(int i = 0; i < nGrid; ++i)
        {
            Basis b;
            b.type = 0;
            b.c = {margin + uni(rng) * W, margin + uni(rng) * H};
            b.theta = uni(rng) * 3.14159265f;
            b.decay = (i == 0) ? 0.0f : (1.2f + 1.8f * uni(rng)) * invArea; // first basis = global
            bases.push_back(b);
        }
        // Several spread-out radial hubs, NOT one. Each is a local "canonical point" that gathers
        // only its own district's traffic; a minimum separation distributes them across the map and
        // a tight decay keeps each one local, so the network has multiple centers instead of every
        // road funnelling into a single unnatural sink.
        const int nHubs = std::max(0, cfg.getInt("sim.proc.hubs", 3)); // canonical traffic centers
        const float hubSep = 0.30f * std::min(W, H);
        std::vector<glm::vec2> hubC;
        for(int tries = 0; tries < nHubs * 8 && static_cast<int>(hubC.size()) < nHubs; ++tries)
        {
            const glm::vec2 c{margin + (0.15f + 0.70f * uni(rng)) * W,
                              margin + (0.15f + 0.70f * uni(rng)) * H};
            bool ok = true;
            for(const glm::vec2& q : hubC)
                if(glm::length(c - q) < hubSep) { ok = false; break; }
            if(!ok) continue;
            hubC.push_back(c);
            Basis b;
            b.type = 1;
            b.c = c;
            b.decay = (9.0f + 5.0f * uni(rng)) * invArea; // tight -> a LOCAL hub, not a global sink
            b.theta = 0.0f;
            bases.push_back(b);
        }

        auto fieldAB = [&](glm::vec2 p) -> glm::vec2 {
            float A = 0.0f, B = 0.0f;
            for(const Basis& b : bases)
            {
                const glm::vec2 v = p - b.c;
                const float r2 = v.x * v.x + v.y * v.y;
                const float w = (b.decay <= 0.0f) ? 1.0f : std::exp(-b.decay * r2);
                if(b.type == 0)
                {
                    A += w * std::cos(2.0f * b.theta);
                    B += w * std::sin(2.0f * b.theta);
                }
                else if(r2 > 1e-3f)
                {
                    A += w * (v.x * v.x - v.y * v.y) / r2;
                    B += w * (2.0f * v.x * v.y) / r2;
                }
            }
            return {A, B};
        };
        // Unit road direction at p (major eigenvector, or its 90-degree minor); {0,0} at a field
        // singularity so the tracer knows to stop.
        auto dirAt = [&](glm::vec2 p, bool major) -> glm::vec2 {
            const glm::vec2 ab = fieldAB(p);
            if(ab.x * ab.x + ab.y * ab.y < 1e-9f)
                return {0.0f, 0.0f};
            float th = 0.5f * std::atan2(ab.y, ab.x);
            if(!major)
                th += 1.5707963f;
            return {std::cos(th), std::sin(th)};
        };
        auto inDom = [&](glm::vec2 p) {
            return p.x >= 0.0f && p.x <= 2.0f * margin + W && p.y >= 0.0f && p.y <= 2.0f * margin + H;
        };

        // ---- Trace a streamline from a seed (RK4, both directions) ----
        // Eigenvectors are sign-ambiguous, so each step takes the sign continuous with the
        // previous heading (dot > 0). Stops at the domain edge, a singularity, or the step cap.
        const float ds = spacing * 0.25f;
        const int maxSteps = static_cast<int>((W + H) / ds) + 8;
        auto signedDir = [&](glm::vec2 p, bool major, glm::vec2 prev) -> glm::vec2 {
            glm::vec2 d = dirAt(p, major);
            if(d.x == 0.0f && d.y == 0.0f)
                return d;
            if(d.x * prev.x + d.y * prev.y < 0.0f)
                d = -d;
            return d;
        };
        auto traceHalf = [&](glm::vec2 seed, bool major, glm::vec2 dir0, std::vector<glm::vec2>& out) {
            glm::vec2 p = seed, prev = dir0;
            for(int s = 0; s < maxSteps; ++s)
            {
                glm::vec2 k1 = signedDir(p, major, prev);
                if(k1.x == 0.0f && k1.y == 0.0f) break;
                glm::vec2 k2 = signedDir(p + k1 * (ds * 0.5f), major, k1);
                if(k2.x == 0.0f && k2.y == 0.0f) k2 = k1;
                glm::vec2 k3 = signedDir(p + k2 * (ds * 0.5f), major, k2);
                if(k3.x == 0.0f && k3.y == 0.0f) k3 = k2;
                glm::vec2 k4 = signedDir(p + k3 * ds, major, k3);
                if(k4.x == 0.0f && k4.y == 0.0f) k4 = k3;
                glm::vec2 step = (k1 + k2 * 2.0f + k3 * 2.0f + k4) * (ds / 6.0f);
                const float sl = std::sqrt(step.x * step.x + step.y * step.y);
                if(sl < 1e-4f) break;
                p = p + step;
                prev = step / sl;
                if(!inDom(p)) break;
                out.push_back(p);
            }
        };
        auto trace = [&](glm::vec2 seed, bool major) -> std::vector<glm::vec2> {
            const glm::vec2 d0 = dirAt(seed, major);
            std::vector<glm::vec2> fwd, bwd, line;
            if(d0.x != 0.0f || d0.y != 0.0f)
            {
                traceHalf(seed, major, d0, fwd);
                traceHalf(seed, major, glm::vec2(-d0.x, -d0.y), bwd);
            }
            for(auto it = bwd.rbegin(); it != bwd.rend(); ++it) line.push_back(*it);
            line.push_back(seed);
            for(const glm::vec2& q : fwd) line.push_back(q);
            return line;
        };

        // ---- Seed streamlines on a coarse grid, skipping seeds too near an existing line so the
        //      spacing (block size) stays even. Majors first (arterials), then minors (local). ----
        std::vector<std::vector<glm::vec2>> majors, minors;
        auto farFrom = [&](glm::vec2 p, const std::vector<std::vector<glm::vec2>>& lines, float sep) {
            const float s2 = sep * sep;
            for(const auto& ln : lines)
                for(const glm::vec2& q : ln)
                {
                    const glm::vec2 d = p - q;
                    if(d.x * d.x + d.y * d.y < s2) return false;
                }
            return true;
        };
        auto seedSet = [&](bool major, float sep, std::vector<std::vector<glm::vec2>>& out) {
            for(float y = margin; y <= margin + H + 1.0f; y += sep)
                for(float x = margin; x <= margin + W + 1.0f; x += sep)
                {
                    const glm::vec2 sp{x, y};
                    if(!farFrom(sp, out, sep * 0.7f)) continue;
                    std::vector<glm::vec2> ln = trace(sp, major);
                    if(ln.size() >= 4) out.push_back(std::move(ln));
                }
        };
        // Street density: arterial (major) + local (minor) spacing, in world metres.
        const float majorSep = std::max(spacing * 0.5f, cfg.getFloat("sim.proc.arterial_spacing_m",
                                                                      spacing * 2.0f));
        const float minorSep = std::max(spacing * 0.3f, cfg.getFloat("sim.proc.street_spacing_m",
                                                                     spacing));
        seedSet(true, majorSep, majors);
        seedSet(false, minorSep, minors);
        if(majors.empty() || minors.empty())
        {
            LOG_ERROR("[city] tensor field produced no streamlines");
            return 0;
        }

        // ---- Intersections: rasterize polylines onto a coarse grid; a cell touched by both a
        //      major and a minor line is a crossing -> one shared node there. ----
        const float cell = spacing * 0.6f;
        auto cellKey = [&](glm::vec2 p) {
            return std::pair<int, int>(static_cast<int>(std::floor(p.x / cell)),
                                       static_cast<int>(std::floor(p.y / cell)));
        };
        // For each cell: which (line, point-index) of each family pass through it.
        std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> majCell, minCell;
        for(int mi = 0; mi < static_cast<int>(majors.size()); ++mi)
            for(int pi = 0; pi < static_cast<int>(majors[mi].size()); ++pi)
                majCell[cellKey(majors[mi][pi])].push_back({mi, pi});
        for(int mj = 0; mj < static_cast<int>(minors.size()); ++mj)
            for(int pj = 0; pj < static_cast<int>(minors[mj].size()); ++pj)
                minCell[cellKey(minors[mj][pj])].push_back({mj, pj});

        struct Cross { int pidx; uint32_t node; };
        std::vector<std::vector<Cross>> majCross(majors.size()), minCross(minors.size());
        std::map<uint32_t, glm::vec2> posOf;
        uint32_t nextNode = 1;
        for(const auto& [k, majList] : majCell)
        {
            auto it = minCell.find(k);
            if(it == minCell.end()) continue; // not a crossing cell
            // Node at the first major point in this cell.
            const glm::vec2 np = majors[majList.front().first][majList.front().second];
            const uint32_t id = nextNode++;
            posOf[id] = np;
            { Node n; n.id = id; n.pos = np; addNode(n); }
            for(const auto& [mi, pi] : majList) majCross[mi].push_back({pi, id});
            for(const auto& [mj, pj] : it->second) minCross[mj].push_back({pj, id});
        }
        if(posOf.empty())
        {
            LOG_ERROR("[city] tensor streamlines never crossed");
            return 0;
        }

        // ---- Build segments: split each line at its crossings into chords between consecutive
        //      distinct nodes. Major lines = ARTERIAL, minor = LOCAL (a free class hierarchy). ----
        uint32_t segId = 0;
        std::vector<std::pair<uint32_t, RoadClass>> twoWay;
        std::set<std::pair<uint32_t, uint32_t>> haveEdge;
        auto build = [&](std::vector<std::vector<Cross>>& crossOf,
                         const std::vector<std::vector<glm::vec2>>& lines, RoadClass cls) {
            for(std::size_t li = 0; li < crossOf.size(); ++li)
            {
                auto& cr = crossOf[li];
                const std::vector<glm::vec2>& line = lines[li];
                std::sort(cr.begin(), cr.end(),
                          [](const Cross& a, const Cross& b) { return a.pidx < b.pidx; });
                for(std::size_t i = 0; i + 1 < cr.size(); ++i)
                {
                    const uint32_t a = cr[i].node, b = cr[i + 1].node;
                    if(a == b) continue;
                    const std::pair<uint32_t, uint32_t> e(std::min(a, b), std::max(a, b));
                    if(haveEdge.count(e)) continue;
                    const glm::vec2 pa = posOf[a], pb = posOf[b];
                    // Curved centerline = the streamline sub-polyline between the two crossings
                    // (endpoints snapped to the node positions). This is what makes the road a
                    // smooth curve following the field instead of a straight chord.
                    std::vector<glm::vec2> cl;
                    cl.push_back(pa);
                    for(int k = cr[i].pidx + 1; k < cr[i + 1].pidx && k < static_cast<int>(line.size());
                        ++k)
                        cl.push_back(line[static_cast<std::size_t>(k)]);
                    cl.push_back(pb);
                    float len = 0.0f;
                    for(std::size_t k = 1; k < cl.size(); ++k)
                        len += glm::length(cl[k] - cl[k - 1]);
                    if(len < spacing * 0.2f) continue; // drop tiny stubs
                    haveEdge.insert(e);
                    const RoadClassSpec spec = classSpec(cls);
                    RoadSegment s{};
                    s.id = segId;
                    s.fromNode = a;
                    s.toNode = b;
                    const float chord = glm::length(pb - pa);
                    s.dir = (chord > 1e-3f) ? (pb - pa) / chord : glm::vec2(1.0f, 0.0f);
                    s.length = len;
                    s.lanes = spec.lanes;
                    s.speedLimit = spec.speed;
                    s.roadClass = cls;
                    if(cl.size() > 2) // >2 points => actually curved; 2 => straight, leave empty
                        s.setCenterline(cl);
                    addSegment(s);
                    twoWay.emplace_back(segId, cls);
                    ++segId;
                }
            }
        };
        build(majCross, majors, RoadClass::ARTERIAL);
        build(minCross, minors, RoadClass::LOCAL);
        for(const auto& [fid, cls] : twoWay)
            makeTwoWay(fid, laneWidth, classSpec(cls).medianWidth);

        classifyJunctions(seed);      // tag 3-way / 4-way / roundabout from node degree
        buildRoundabouts(laneWidth);  // turn roundabout nodes into real circulating rings
        makeSomeOneWay(cfg.getFloat("sim.proc.oneway_frac", 0.15f), seed); // one-way couplets

        LOG_INFO("generated tensor-field city: {n} directed segments, {nd} junctions",
                 PARAM(n, m_segments.size()), PARAM(nd, posOf.size()));
        return m_segments.size();
    }

    void RoadNetwork::buildRoundabouts(float laneWidth)
    {
        // Snapshot roundabout centre ids first (we add nodes/segments below).
        std::vector<uint32_t> centers;
        for(const auto& [id, n] : m_nodes)
            if(n.junction == JunctionStyle::ROUNDABOUT)
                centers.push_back(id);
        std::sort(centers.begin(), centers.end());
        uint32_t nextNode = 0, nextSeg = 0;
        for(const auto& [id, n] : m_nodes)
            nextNode = std::max(nextNode, id);
        for(const auto& [id, s] : m_segments)
            nextSeg = std::max(nextSeg, id);
        ++nextNode;
        ++nextSeg;

        // Move segment `sid`'s endpoint that currently == oldN to newN, fixing the node
        // incoming/outgoing lists, the segment dir/length, and the render visual endpoint.
        auto rewire = [&](uint32_t sid, uint32_t oldN, uint32_t newN) {
            RoadSegment* s = getSegment(sid);
            if(!s)
                return;
            const bool fromMoved = (s->fromNode == oldN);
            if(fromMoved)
                s->fromNode = newN;
            else if(s->toNode == oldN)
                s->toNode = newN;
            else
                return;
            if(Node* oN = getNode(oldN))
            {
                auto& lst = fromMoved ? oN->outgoing : oN->incoming;
                lst.erase(std::remove(lst.begin(), lst.end(), sid), lst.end());
            }
            if(Node* nN = getNode(newN))
                (fromMoved ? nN->outgoing : nN->incoming).push_back(sid);
            const Node* a = getNode(s->fromNode);
            const Node* b = getNode(s->toNode);
            if(a && b)
            {
                const glm::vec2 v = b->pos - a->pos;
                const float L = glm::length(v);
                if(L > 1e-3f)
                {
                    s->dir = v / L;
                    s->length = L;
                }
            }
            for(auto& vis : m_seg)
                if(vis.id == sid)
                {
                    if(const Node* nn = getNode(newN))
                    {
                        if(fromMoved)
                        {
                            vis.x1 = static_cast<int>(nn->pos.x);
                            vis.y1 = static_cast<int>(nn->pos.y);
                        }
                        else
                        {
                            vis.x2 = static_cast<int>(nn->pos.x);
                            vis.y2 = static_cast<int>(nn->pos.y);
                        }
                        const float dx = static_cast<float>(vis.x2 - vis.x1);
                        const float dy = static_cast<float>(vis.y2 - vis.y1);
                        vis.length = std::sqrt(dx * dx + dy * dy);
                    }
                    break;
                }
        };

        for(uint32_t R : centers)
        {
            const Node* rn = getNode(R);
            if(!rn)
                continue;
            const glm::vec2 Rc = rn->pos;
            // Group incident directed segments by their OTHER node (a two-way arm = fwd+rev pair).
            std::map<uint32_t, std::vector<uint32_t>> arms; // other node -> incident segment ids
            for(const auto& [sid, s] : m_segments)
            {
                if(s.fromNode == R && s.toNode != R)
                    arms[s.toNode].push_back(sid);
                else if(s.toNode == R && s.fromNode != R)
                    arms[s.fromNode].push_back(sid);
            }
            if(arms.size() < 3)
                continue; // too few arms — leave it as a visual-only roundabout

            // Ring radius grows with arm count but is capped to a fraction of the SHORTEST arm so
            // a ring node never lands past its road's far end (which would invert the road).
            float minArm = 1e30f;
            for(const auto& [other, segs] : arms)
            {
                (void)segs;
                if(const Node* on = getNode(other))
                    minArm = std::min(minArm, glm::length(on->pos - Rc));
            }
            const float r = std::max(laneWidth * 2.5f,
                                     std::min(laneWidth * 1.1f * static_cast<float>(arms.size()),
                                              0.35f * minArm));
            struct RingNode { uint32_t id; float ang; };
            std::vector<RingNode> ring;
            for(const auto& [other, segs] : arms) // map: sorted by `other` -> deterministic
            {
                const Node* on = getNode(other);
                if(!on)
                    continue;
                glm::vec2 d = on->pos - Rc;
                const float L = glm::length(d);
                if(L < 1e-3f)
                    continue;
                d /= L;
                const uint32_t rid = nextNode++;
                Node nn;
                nn.id = rid;
                nn.pos = Rc + d * r;
                nn.junction = JunctionStyle::RING;
                addNode(nn);
                for(uint32_t sid : segs)
                    rewire(sid, R, rid);
                ring.push_back({rid, std::atan2(d.y, d.x)});
            }
            if(ring.size() < 3)
                continue;
            std::sort(ring.begin(), ring.end(),
                      [](const RingNode& a, const RingNode& b) { return a.ang < b.ang; });
            // One-way circulating loop through the ring nodes (counter-clockwise by angle).
            for(std::size_t i = 0; i < ring.size(); ++i)
            {
                const uint32_t a = ring[i].id, b = ring[(i + 1) % ring.size()].id;
                const Node* na = getNode(a);
                const Node* nb = getNode(b);
                if(!na || !nb)
                    continue;
                const glm::vec2 v = nb->pos - na->pos;
                const float L = glm::length(v);
                if(L < 1e-3f)
                    continue;
                RoadSegment s{};
                s.id = nextSeg++;
                s.fromNode = a;
                s.toNode = b;
                s.dir = v / L;
                s.length = L;
                s.lanes = 1;
                s.speedLimit = 8.0f; // slow circulation
                s.oneway = true;
                s.roadClass = RoadClass::COLLECTOR;
                addSegment(s);
            }
            if(Node* c = getNode(R)) // re-fetch (maps may have rehashed)
                c->roundaboutR = r;  // now isolated; the island is drawn here
        }
    }

    void RoadNetwork::classifyJunctions(uint64_t seed)
    {
        std::mt19937_64 rng(seed ^ 0x5DEECE66DULL);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for(uint32_t id : getNodeIds()) // sorted -> deterministic draw order
        {
            Node* n = getNode(id);
            if(!n)
                continue;
            // Incident road count. In a two-way network each road is one incoming + one outgoing,
            // so this is the number of streets meeting here.
            const int deg = static_cast<int>(std::max(n->incoming.size(), n->outgoing.size()));
            if(deg >= 5)
                n->junction = (u(rng) < 0.85f) ? JunctionStyle::ROUNDABOUT : JunctionStyle::FOUR_WAY;
            else if(deg == 4)
                n->junction = (u(rng) < 0.15f) ? JunctionStyle::ROUNDABOUT : JunctionStyle::FOUR_WAY;
            else if(deg == 3)
                n->junction = (u(rng) < 0.05f) ? JunctionStyle::ROUNDABOUT : JunctionStyle::THREE_WAY;
            else
                n->junction = JunctionStyle::PLAIN; // <=2 roads: through-point or dead-end
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
            if(node.junction == JunctionStyle::ROUNDABOUT || node.junction == JunctionStyle::RING)
                continue; // roundabouts (and their ring nodes) yield, they don't signalize
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