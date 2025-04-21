#include "alerts/AlertManager.hpp"
#include <iostream>

namespace tfv
{
    AlertManager::AlertManager(Simulation& sim) : m_sim(sim)
    {
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
        m_sim.enableAlerts(enabled);

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

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            // Check if we already have an active alert for this segment and type
            for(const auto& alert : m_alerts)
            {
                if(alert.segmentId == segmentId && alert.type == type && !alert.acknowledged)
                {
                    // We already have an active alert, don't add another one
                    return;
                }
            }

            // Add new alert
            m_alerts.emplace_front(type, segmentId, message);

            // Keep only the maximum number of alerts
            if(m_alerts.size() > MAX_ALERTS)
            {
                m_alerts.pop_back();
            }
        }

        // Call the callback with the new alert
        if(m_callback)
        {
            m_callback(m_alerts.front());
        }

        // Log the alert
        std::cout << "[Alert] " << message << std::endl;
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
} // namespace tfv