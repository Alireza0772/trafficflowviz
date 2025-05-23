#include "core/Simulation.hpp"
#include "utils/LoggingManager.hpp"
#include <algorithm>
#include <data/CSVLoader.hpp>
#include <glm/glm.hpp>
#include <iostream>
#include <unordered_set>

namespace tfv
{
    Simulation::Simulation(RoadNetwork* net) : m_roadNetwork(net)
    {
        // Initialize alert thresholds
        m_alertThresholds[AlertType::CONGESTION] = 0.7f;       // 70% congestion
        m_alertThresholds[AlertType::SPEED_VIOLATION] = 1.5f;  // 50% over limit
        m_alertThresholds[AlertType::UNUSUAL_SLOWDOWN] = 0.5f; // 50% below average
        m_alertThresholds[AlertType::INCIDENT] = 0.8f;         // 80% drop in speed
    }

    bool Simulation::initialize(const std::filesystem::path& cityInformationPath,
                                const std::filesystem::path& vehicleInformationPath)
    {
        std::scoped_lock lock(m_mtx);
        // Clear previous data
        m_vehicles.clear();
        m_segmentStats.clear();
        m_speedLimits.clear();
        m_timeSinceLastUpdate = 0.0;

        // Load road network
        if(!m_roadNetwork)
        {
            m_roadNetwork = new RoadNetwork();
            if(!m_roadNetwork->loadCSV(cityInformationPath))
            {
                LOG_ERROR("Failed to load road network from {file}",
                          PARAM(file, cityInformationPath.string()));
                return false;
            }
        }
        // Load vehicle information
        auto vehicles = tfv::loadVehiclesCSV(vehicleInformationPath);
        if(vehicles.empty())
        {
            LOG_ERROR("Failed to load vehicle information from {file}",
                      PARAM(file, vehicleInformationPath.string()));
            return false;
        }

        for(const auto& v : vehicles)
        {
            m_vehicles[v.id] = v;

            // Update congestion for the segment
            if(m_roadNetwork)
            {
                auto* segment = m_roadNetwork->getSegment(v.segmentId);
                if(segment)
                {
                    segment->vehicleCount++;
                    updateCongestion(v.segmentId);

                    LOG_INFO("Segment {segmentId} now has {count} vehicles",
                             PARAM(segmentId, v.segmentId), PARAM(count, segment->vehicleCount));
                }
            }
        }

        LOG_INFO("Initialized {count} vehicles in the simulation.", PARAM(count, vehicles.size()));

        return true;
    }

    void Simulation::update(double dt)
    {
        std::scoped_lock lock(m_mtx);

        // Update time since last statistics update
        m_timeSinceLastUpdate += dt;

        // Update vehicle positions
        for(auto& [id, v] : m_vehicles)
        {
            if(!m_roadNetwork)
                continue;

            // Get the road segment for this vehicle
            auto segment = m_roadNetwork->getSegment(v.segmentId);
            if(!segment)
                continue;

            // Adjust speed based on congestion
            float speedFactor = 1.0f - segment->congestionLevel * 0.8f;

            // Move vehicle along its segment by its velocity's magnitude
            float speed = glm::length(v.vel) * speedFactor;
            float distance = speed * static_cast<float>(dt);
            float prevPosition = v.position;
            v.position += distance / segment->length; // position is 0..1 along segment

            // If vehicle passes end of segment, move to next segment
            if(v.position > 1.f)
            {
                if(m_roadNetwork)
                {
                    // Get the next segment from the road network
                    const auto* fromNode = m_roadNetwork->getNode(segment->toNode);
                    if(fromNode && !fromNode->outgoing.empty())
                    {
                        // Choose a random outgoing segment
                        size_t nextIdx = rand() % fromNode->outgoing.size();
                        uint32_t nextSegmentId = fromNode->outgoing[nextIdx];

                        // Update the vehicle's segment and reset position
                        v.segmentId = nextSegmentId;
                        v.position = v.position - 1.f; // Carry over extra distance
                    }
                    else
                    {
                        // No outgoing segments, loop back to beginning
                        v.position -= 1.f;
                    }
                }
                else
                {
                    // No road network, simple loop for demo
                    v.position -= 1.f;
                }
            }

            // Record current speed for segment statistics
            segment->currentSpeed = speed;
        }

        // Update segment statistics and congestion levels periodically
        if(m_timeSinceLastUpdate >= m_statUpdateInterval)
        {
            // Count vehicles per segment
            std::unordered_map<uint32_t, int> vehiclesPerSegment;
            std::unordered_map<uint32_t, float> avgSpeedPerSegment;

            for(const auto& [id, v] : m_vehicles)
            {
                vehiclesPerSegment[v.segmentId]++;
                avgSpeedPerSegment[v.segmentId] += glm::length(v.vel);
            }

            // Calculate average speeds and update congestion levels
            for(auto& [segmentId, count] : vehiclesPerSegment)
            {
                if(count > 0)
                {
                    avgSpeedPerSegment[segmentId] /= count;
                }

                // Update congestion for this segment
                updateCongestion(segmentId);

                // Update segment statistics
                if(m_segmentStats.find(segmentId) == m_segmentStats.end())
                {
                    m_segmentStats[segmentId] = SegmentStatistics{};
                }

                m_segmentStats[segmentId].addSample(avgSpeedPerSegment[segmentId], count);
            }

            // Check for alert conditions
            if(m_alertsEnabled)
            {
                checkAlerts();
            }

            m_timeSinceLastUpdate = 0.0;
        }
    }

    VehicleMap Simulation::snapshot() const
    {
        std::scoped_lock lock(m_mtx);
        return m_vehicles; // copy
    }

    SegmentStatsMap Simulation::getSegmentStats() const
    {
        std::scoped_lock lock(m_mtx);
        return m_segmentStats; // copy
    }

    std::unordered_map<uint32_t, float> Simulation::getCongestionLevels() const
    {
        std::scoped_lock lock(m_mtx);
        std::unordered_map<uint32_t, float> result;

        if(!m_roadNetwork)
            return result;

        for(uint32_t segmentId : m_roadNetwork->getSegmentIds())
        {
            const auto* segment = m_roadNetwork->getSegment(segmentId);
            if(segment)
            {
                result[segmentId] = segment->congestionLevel;
            }
        }

        return result;
    }

    void Simulation::addVehicle(const Vehicle& v)
    {
        std::scoped_lock lock(m_mtx);
        LOG_DEBUG("Adding vehicle {id}", PARAM(id, v.id));
        m_vehicles[v.id] = v;

        // Update congestion for the segment
        if(m_roadNetwork)
        {
            auto* segment = m_roadNetwork->getSegment(v.segmentId);
            if(segment)
            {
                segment->vehicleCount++;
                updateCongestion(v.segmentId);
            }
        }
    }

    void Simulation::removeVehicle(uint64_t id)
    {
        std::scoped_lock lock(m_mtx);
        LOG_DEBUG("Removing vehicle {id}", PARAM(id, id));

        // Update segment vehicle count
        auto it = m_vehicles.find(id);
        if(it != m_vehicles.end() && m_roadNetwork)
        {
            uint32_t segmentId = it->second.segmentId;
            auto* segment = m_roadNetwork->getSegment(segmentId);
            if(segment && segment->vehicleCount > 0)
            {
                segment->vehicleCount--;
                updateCongestion(segmentId);
            }
        }

        m_vehicles.erase(id);
    }

    void Simulation::setSpeedLimit(uint32_t segmentId, float limit)
    {
        std::scoped_lock lock(m_mtx);
        m_speedLimits[segmentId] = limit;
    }

    float Simulation::getSpeedLimit(uint32_t segmentId) const
    {
        std::scoped_lock lock(m_mtx);
        auto it = m_speedLimits.find(segmentId);
        if(it != m_speedLimits.end())
        {
            return it->second;
        }

        // Default speed limit if not specified
        return 13.9f; // ~50 km/h
    }

    void Simulation::setAlertThreshold(AlertType type, float threshold)
    {
        std::scoped_lock lock(m_mtx);
        m_alertThresholds[type] = threshold;
    }

    void Simulation::updateCongestion(uint32_t segmentId)
    {
        LOG_DEBUG("Updating congestion for segment {segmentId}", PARAM(segmentId, segmentId));
        if(!m_roadNetwork)
            return;

        auto* segment = m_roadNetwork->getSegment(segmentId);
        if(!segment)
            return;

        // Simple congestion model: vehicle count / segment length
        float capacity = segment->length / 10.0f; // 1 vehicle per 10 meters at max capacity
        float congestionLevel = static_cast<float>(segment->vehicleCount) / capacity;

        // Clamp to 0-1 range
        congestionLevel = std::max(0.0f, std::min(congestionLevel, 1.0f));

        // Update segment congestion level
        segment->congestionLevel = congestionLevel;
    }

    void Simulation::checkAlerts()
    {
        if(!m_roadNetwork || !m_alertCallback)
            return;

        for(uint32_t segmentId : m_roadNetwork->getSegmentIds())
        {
            const auto* segment = m_roadNetwork->getSegment(segmentId);
            if(!segment)
                continue;

            // Check for congestion
            if(segment->congestionLevel >= m_alertThresholds[AlertType::CONGESTION])
            {
                std::string message =
                    "Heavy traffic detected on road segment " + std::to_string(segmentId);
                m_alertCallback(AlertType::CONGESTION, segmentId, message);
            }

            // Check for unusual slowdown
            auto statsIt = m_segmentStats.find(segmentId);
            if(statsIt != m_segmentStats.end())
            {
                const auto& stats = statsIt->second;
                if(stats.speedHistory.size() > 1)
                {
                    float currentSpeed = segment->currentSpeed;
                    float avgSpeed = stats.avgSpeed;

                    if(avgSpeed > 0 &&
                       currentSpeed < avgSpeed * m_alertThresholds[AlertType::UNUSUAL_SLOWDOWN])
                    {
                        std::string message = "Unusual slowdown detected on road segment " +
                                              std::to_string(segmentId);
                        m_alertCallback(AlertType::UNUSUAL_SLOWDOWN, segmentId, message);
                    }
                }
            }
        }
    }

} // namespace tfv