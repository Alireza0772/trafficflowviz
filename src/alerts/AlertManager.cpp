#include "alerts/AlertManager.hpp"
#include "utils/LoggingManager.hpp"
#include <iostream>

namespace tfv
{
    AlertManager::AlertManager(Simulation& sim) : m_sim(sim)
    {
        // Set default thresholds
        setThreshold(AlertType::CONGESTION, 0.7f);
        setThreshold(AlertType::SPEED_VIOLATION, 1.5f);
        setThreshold(AlertType::UNUSUAL_SLOWDOWN, 0.5f);

        // Set up the simulation callback
        m_sim.setAlertCallback(
            [this](AlertType type, uint32_t segmentId, const std::string& message)
            { this->addAlert(type, segmentId, message); });
    }

    AlertManager::~AlertManager()
    {
        // Remove the callback from simulation
        m_sim.setAlertCallback(nullptr);
    }

    void AlertManager::setEnabled(bool enabled)
    {
        m_enabled = enabled;
        m_sim.setEnabled(enabled);

        if(!enabled)
        {
            // Clear all pending alerts
            std::lock_guard<std::mutex> lock(m_mutex);
            m_alerts.clear();
        }
    }

    void AlertManager::addAlert(AlertType type, uint32_t segmentId, const std::string& message)
    {
        if(!m_enabled)
            return;

        std::lock_guard<std::mutex> lock(m_mutex);

        // Create a new alert
        Alert alert(type, segmentId, message);

        // Trim alerts if we have too many
        if(m_alerts.size() >= MAX_ALERTS)
        {
            m_alerts.pop_front();
        }

        // Add the new alert
        m_alerts.push_back(alert);

        // Notify via callback if set
        if(m_callback)
        {
            m_callback(alert);
        }

        // Log the alert
        emitAlert(message);
    }

    void AlertManager::acknowledgeAlert(size_t index)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if(index < m_alerts.size())
        {
            m_alerts[index].acknowledged = true;
        }
    }

    std::vector<Alert> AlertManager::getActiveAlerts() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::vector<Alert> activeAlerts;
        for(const auto& alert : m_alerts)
        {
            if(!alert.acknowledged)
            {
                activeAlerts.push_back(alert);
            }
        }

        return activeAlerts;
    }

    void AlertManager::setThreshold(AlertType type, float threshold)
    {
        m_sim.setAlertThreshold(type, threshold);
    }

    void AlertManager::emitAlert(const std::string& message)
    {
        LOG_INFO("[Alert] {message}", PARAM(message, message));
    }
} // namespace tfv