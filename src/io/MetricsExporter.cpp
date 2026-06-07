#include "io/MetricsExporter.hpp"

#include "core/Simulation.hpp"
#include "core/TrafficEntity.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace tfv
{
    namespace
    {
        std::string f(double v, int prec)
        {
            if(v == 0.0)
                v = 0.0; // normalize -0.0 -> +0.0 for cross-build byte stability
            char b[64];
            std::snprintf(b, sizeof(b), "%.*f", prec, v);
            return b;
        }
    } // namespace

    MetricsExporter::MetricsExporter(std::ostream& os, int everyN)
        : m_os(&os), m_everyN(everyN < 1 ? 1 : everyN)
    {
    }

    MetricsExporter::MetricsExporter(const std::string& path, int everyN)
        : m_file(path), m_everyN(everyN < 1 ? 1 : everyN)
    {
        if(m_file.is_open())
            m_os = &m_file;
    }

    void MetricsExporter::writeHeader()
    {
        *m_os << "tick,sim_time,segment_id,avg_speed,avg_density,congestion\n";
        m_headerWritten = true;
    }

    void MetricsExporter::sample(uint64_t tick, double simTime, const Simulation& sim)
    {
        if(!m_os)
            return;
        if(m_everyN > 1 && (tick % static_cast<uint64_t>(m_everyN)) != 0)
            return;
        if(!m_headerWritten)
            writeHeader();

        const auto stats = sim.getSegmentStats();
        const auto congestion = sim.getCongestionLevels();
        std::vector<uint32_t> ids;
        ids.reserve(stats.size());
        for(const auto& [id, s] : stats)
        {
            (void)s;
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());

        for(uint32_t id : ids)
        {
            const SegmentStatistics& s = stats.at(id);
            float cong = 0.0f;
            auto it = congestion.find(id);
            if(it != congestion.end())
                cong = it->second;
            *m_os << tick << ',' << f(simTime, 3) << ',' << id << ',' << f(s.avgSpeed, 4) << ','
                  << f(s.avgDensity, 4) << ',' << f(cong, 4) << '\n';
        }
    }

} // namespace tfv
