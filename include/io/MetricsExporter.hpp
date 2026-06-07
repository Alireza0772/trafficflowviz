#ifndef TFV_METRICS_EXPORTER_HPP
#define TFV_METRICS_EXPORTER_HPP

#include <cstdint>
#include <fstream>
#include <ostream>
#include <string>

namespace tfv
{
    class Simulation;

    /**
     * Streams per-segment metrics over time to CSV (a thin read-only tap on
     * getSegmentStats() + getCongestionLevels()). Same discipline as TrajectoryExporter:
     * read-only, deterministic, ascending-segment-id order, ctor-parameterized.
     */
    class MetricsExporter
    {
      public:
        explicit MetricsExporter(std::ostream& os, int everyN = 1);
        explicit MetricsExporter(const std::string& path, int everyN = 1);

        void sample(uint64_t tick, double simTime, const Simulation& sim);
        bool ok() const { return m_os != nullptr; }

      private:
        void writeHeader();

        std::ofstream m_file;
        std::ostream* m_os{nullptr};
        int m_everyN{1};
        bool m_headerWritten{false};
    };

} // namespace tfv

#endif // TFV_METRICS_EXPORTER_HPP
