#ifndef TFV_ALERT_MANAGER_HPP
#define TFV_ALERT_MANAGER_HPP

#include "core/Simulation.hpp"
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace tfv
{
    /**
     * Struct representing a traffic alert
     */
    struct Alert
    {
        AlertType type;
        uint32_t segmentId;
        std::string message;
        std::chrono::system_clock::time_point timestamp;
        bool acknowledged{false};

        Alert(AlertType t, uint32_t id, std::string msg)
            : type(t), segmentId(id), message(std::move(msg)),
              timestamp(std::chrono::system_clock::now())
        {
        }
    };

    /**
     * Manages traffic alerts and notifications
     */
    class AlertManager
    {
      public:
        AlertManager(Simulation& sim);
        ~AlertManager();

        /**
         * Enable or disable the alert system
         */
        void setEnabled(bool enabled);

        /**
         * Process a new alert
         */
        void addAlert(AlertType type, uint32_t segmentId, const std::string& message);

        /**
         * Acknowledge an alert by index
         */
        void acknowledgeAlert(size_t index);

        /**
         * Get all active alerts
         */
        std::vector<Alert> getActiveAlerts() const;

        /**
         * Set callback for when new alerts are received
         */
        using AlertCallback = std::function<void(const Alert& alert)>;
        void setAlertCallback(AlertCallback callback) { m_callback = callback; }

        /**
         * Set threshold for an alert type (0.0-1.0)
         */
        void setThreshold(AlertType type, float threshold);

        /**
         * Emit an alert message
         */
        void emitAlert(const std::string& message);

      private:
        Simulation& m_sim;
        bool m_enabled{false};
        std::deque<Alert> m_alerts;
        AlertCallback m_callback;
        mutable std::mutex m_mutex;

        // Maximum number of alerts to keep
        static constexpr size_t MAX_ALERTS = 100;
    };
} // namespace tfv

#endif