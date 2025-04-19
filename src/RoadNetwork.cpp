#include "RoadNetwork.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace tfv
{

    bool RoadNetwork::loadCSV(const std::filesystem::path& path)
    {
        m_seg.clear();
        m_adj.clear(); // Initialize adjacency list

        std::ifstream file(path);
        if(!file.is_open())
        {
            std::cerr << "[Road] could not open " << path << '\n';
            return false;
        }

        std::string line;
        std::getline(file, line); // skip header

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

                m_adj[r.x1].push_back(r.id);
                m_adj[r.x2].push_back(r.id);
            }
        }

        std::cout << "[Road] loaded " << m_seg.size() << " segments from " << path << '\n';
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

} // namespace tfv