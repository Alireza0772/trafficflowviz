#ifndef TFV_SIMULATION_HPP
#define TFV_SIMULATION_HPP

#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <unordered_set>
#include <vector>

#include "agents/Brain.hpp"
#include "agents/IdmParams.hpp"
#include "control/LightController.hpp"
#include "core/RoadNetwork.hpp"
#include "core/TrafficEntity.hpp"
#include "core/World.hpp"
#include "perception/PerceptionSystem.hpp"

#include <array>

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

        /** Programmatic initialization: uses the already-set road network and the
         *  given vehicles (no CSV). Used by tests and headless runs. */
        bool initialize(std::vector<Vehicle> vehicles);

        /** Advance physics by `dt` seconds. */
        void update(double dt);

        /** Thread‑safe copy for rendering. */
        VehicleMap snapshot() const;

        /** Number of vehicles (cheap; avoids a full snapshot copy just to count). */
        std::size_t vehicleCount() const;

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

        // Build the normalized observation for a vehicle. leaderIdx<0 = no same-segment
        // leader; (crossGapNorm,crossRelSpeed,hasCross) is the cross-segment front
        // override; sectors are the world-space F/R/L/R neighbours; dt is the tick.
        Observation buildObservation(const Vehicle& self, long leaderIdx,
                                     const std::vector<Vehicle>& vehs,
                                     const std::array<SensedNeighbor, 4>& sectors,
                                     float crossGapNorm, float crossRelSpeed, bool hasCross,
                                     double dt) const;

        // Deterministic READ-ONLY next-segment predicate (mirrors the Phase-B hand-off
        // without advancing routeIdx), for cross-segment leader lookahead.
        uint32_t nextSegmentForLookahead(const Vehicle& v, const RoadSegment& seg) const;

        // Nearest STOP/YIELD sign ahead of the vehicle on its current segment (null if none).
        const Sign* nearestStopAhead(const Vehicle& v) const;

        // World-space position of `position` (0..1) along a segment centerline.
        glm::vec2 segWorldPos(const RoadSegment& seg, float position) const;

        World m_world;
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

        // Seeded RNG stream for deterministic routing (replaces global rand()).
        std::mt19937 m_rng;

        // Agent decision layer (Phase 2)
        std::unique_ptr<IBrain> m_brain;            // current decision brain (default: rule)
        IdmParams m_idm;                            // model params (also used for accel clamp)
        std::unordered_map<uint64_t, Action> m_lastAction; // held action per vehicle id
        std::unordered_set<uint64_t> m_forceDecide; // ids that must re-decide next tick (handoff)
        std::unordered_map<uint64_t, uint32_t> m_signCleared; // id -> stop/yield sign id cleared on its segment
        uint64_t m_tick{0};                         // simulation tick counter
        float m_decisionHz{10.0f};                  // brain decision rate (Hz)

        // Central traffic-light controller (Phase 4)
        LightController m_lightController;

        // Perception (Phase 5)
        UniformGrid m_grid;
        PerceptionParams m_perceptionParams;
        bool m_crossSegmentLeader{true}; // S3 behavioral toggle (gates the digest change)
    };

} // namespace tfv
#endif