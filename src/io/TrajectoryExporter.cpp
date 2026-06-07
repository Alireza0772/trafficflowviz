#include "io/TrajectoryExporter.hpp"

#include "agents/Action.hpp"
#include "core/Simulation.hpp"
#include "core/TrafficEntity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <vector>

namespace tfv
{
    namespace
    {
        // Locale-independent fixed-precision float -> string (deterministic bytes).
        std::string f(double v, int prec)
        {
            if(v == 0.0)
                v = 0.0; // normalize -0.0 -> +0.0 for cross-build byte stability
            char b[64];
            std::snprintf(b, sizeof(b), "%.*f", prec, v);
            return b;
        }
    } // namespace

    TrajectoryExporter::TrajectoryExporter(std::ostream& os, int everyN)
        : m_os(&os), m_everyN(everyN < 1 ? 1 : everyN)
    {
    }

    TrajectoryExporter::TrajectoryExporter(const std::string& path, int everyN)
        : m_file(path), m_everyN(everyN < 1 ? 1 : everyN)
    {
        if(m_file.is_open())
            m_os = &m_file;
    }

    void TrajectoryExporter::writeHeader()
    {
        *m_os << "tick,sim_time,vehicle_id,segment_id,lane_index,position,world_x,world_y,"
                 "heading,speed,light_bits,act_accel,act_lane_change,act_turn,act_light_cmd,"
                 "violations\n";
        m_headerWritten = true;
    }

    void TrajectoryExporter::sample(uint64_t tick, double simTime, const Simulation& sim)
    {
        if(!m_os)
            return;
        if(m_everyN > 1 && (tick % static_cast<uint64_t>(m_everyN)) != 0)
            return;
        if(!m_headerWritten)
            writeHeader();

        const auto snap = sim.snapshot();
        const auto acts = sim.lastActions();
        std::vector<uint64_t> ids;
        ids.reserve(snap.size());
        for(const auto& [id, v] : snap)
        {
            (void)v;
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end()); // deterministic row order (VehicleMap is unordered)

        for(uint64_t id : ids)
        {
            const Vehicle& v = snap.at(id);
            Action a{};
            auto it = acts.find(id);
            if(it != acts.end())
                a = it->second;
            const float speed = glm::length(v.vel);
            *m_os << tick << ',' << f(simTime, 3) << ',' << id << ',' << v.segmentId << ','
                  << static_cast<int>(v.laneIndex) << ',' << f(v.position, 6) << ','
                  << f(v.worldPos.x, 6) << ',' << f(v.worldPos.y, 6) << ',' << f(v.heading, 6)
                  << ',' << f(speed, 4) << ',' << static_cast<int>(v.lightBits) << ','
                  << f(a.accel, 4) << ',' << static_cast<int>(a.laneChange) << ','
                  << static_cast<int>(a.turn) << ',' << static_cast<int>(a.lightCmd) << ','
                  << sim.violationCount(id) << '\n';
        }
    }

} // namespace tfv
