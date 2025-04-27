#ifndef TFV_SIMULATION_HPP
#define TFV_SIMULATION_HPP

#include <functional>
#include <mutex>

#include "core/RoadNetwork.hpp"
#include "core/TrafficEntity.hpp"

namespace tfv
{
    // Alert types for event notification
    enum class AlertType
    {
        CONGESTION,       // Traffic congestion on a segment
        SPEED_VIOLATION,  // Vehicle exceeding speed limit
        UNUSUAL_SLOWDOWN, // Unexpected slowdown
        INCIDENT          // Potential incident detection
    };

    // Alert callback signature
    using AlertCallback =
        std::function<void(AlertType type, uint32_t segmentId, const std::string& message)>;

    class Simulation
    {
      public:
        explicit Simulation(RoadNetwork* net = nullptr);

        bool initialize(const std::filesystem::path& cityInformationPath,
                        const std::filesystem::path& vehicleInformationPath);

        /** Advance physics by `dt` seconds. */
        void update(double dt);

        /** Thread‑safe copy for rendering. */
        VehicleMap snapshot() const;

        /** Get segment statistics for visualization */
        SegmentStatsMap getSegmentStats() const;

        /** Get current congestion levels */
        std::unordered_map<uint32_t, float> getCongestionLevels() const;

        // — Mutation API —
        void addVehicle(const Vehicle& v);
        void removeVehicle(uint64_t id);

        // Traffic rule parameters
        void setSpeedLimit(uint32_t segmentId, float limit);
        float getSpeedLimit(uint32_t segmentId) const;

        // Alert system
        void setAlertCallback(AlertCallback cb) { m_alertCallback = cb; }
        void enableAlerts(bool enable) { m_alertsEnabled = enable; }
        void setAlertThreshold(AlertType type, float threshold);
        void setEnabled(bool enable) { m_alertsEnabled = enable; }

        // Get road network
        const RoadNetwork* getRoadNetwork() const { return m_roadNetwork; }

      private:
        // attach inputs to the simulation
        void attachInputs();

        // Update congestion level for a segment
        void updateCongestion(uint32_t segmentId);

        // Check for alert conditions
        void checkAlerts();

        VehicleMap m_vehicles;
        RoadNetwork* m_roadNetwork{nullptr};
        SegmentStatsMap m_segmentStats;
        std::unordered_map<uint32_t, float> m_speedLimits;
        mutable std::mutex m_mtx;

        // Alert system
        AlertCallback m_alertCallback;
        bool m_alertsEnabled{false};
        std::unordered_map<AlertType, float> m_alertThresholds;

        // Update frequency (don't update every frame)
        double m_statUpdateInterval{1.0}; // seconds
        double m_timeSinceLastUpdate{0.0};
    };

} // namespace tfv
#endif