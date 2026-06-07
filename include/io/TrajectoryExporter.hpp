#ifndef TFV_TRAJECTORY_EXPORTER_HPP
#define TFV_TRAJECTORY_EXPORTER_HPP

#include <cstdint>
#include <fstream>
#include <ostream>
#include <string>

namespace tfv
{
    class Simulation;

    /**
     * Streams per-tick, per-vehicle state + held-action rows to CSV. READ-ONLY over the
     * simulation (taps snapshot()/lastActions()/violationCount()), so it is fully
     * behavior-neutral. The format and sampling rate are CONSTRUCTOR parameters (never
     * read from Configuration) and floats are written locale-independently, so the byte
     * output is deterministic and pinned by the golden test. Vehicles are emitted in
     * ascending-id order each tick.
     *
     * Driver contract: call sample(tick, simTime, sim) AFTER sim.update(dt) returns
     * (never inside update — the sim mutex is non-recursive).
     */
    class TrajectoryExporter
    {
      public:
        explicit TrajectoryExporter(std::ostream& os, int everyN = 1);
        explicit TrajectoryExporter(const std::string& path, int everyN = 1);

        void sample(uint64_t tick, double simTime, const Simulation& sim);
        bool ok() const { return m_os != nullptr; }

      private:
        void writeHeader();

        std::ofstream m_file; // owned when constructed from a path
        std::ostream* m_os{nullptr};
        int m_everyN{1};
        bool m_headerWritten{false};
    };

} // namespace tfv

#endif // TFV_TRAJECTORY_EXPORTER_HPP
