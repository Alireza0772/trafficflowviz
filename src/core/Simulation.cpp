#include "core/Simulation.hpp"
#include <algorithm>
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

    void Simulation::step(double dt)
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

        for(const auto& segmentId : m_roadNetwork->getSegmentIds())
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

        // Default speed limit if not set
        return 13.9f; // ~50 km/h in m/s
    }

    void Simulation::setAlertThreshold(AlertType type, float threshold)
    {
        std::scoped_lock lock(m_mtx);
        m_alertThresholds[type] = threshold;
    }

    void Simulation::updateCongestion(uint32_t segmentId)
    {
        if(!m_roadNetwork)
            return;

        auto* segment = m_roadNetwork->getSegment(segmentId);
        if(!segment)
            return;

        // Simple congestion model:
        // - Each lane can handle X vehicles efficiently
        // - As vehicles increase beyond that, congestion increases
        const int VEHICLES_PER_LANE = 5;
        int capacity = segment->laneCount * VEHICLES_PER_LANE;

        if(segment->vehicleCount <= capacity)
        {
            // Linear congestion up to capacity
            segment->congestionLevel = static_cast<float>(segment->vehicleCount) / capacity;
        }
        else
        {
            // Exponential congestion beyond capacity
            float overCapacity = static_cast<float>(segment->vehicleCount - capacity) / capacity;
            segment->congestionLevel = std::min(1.0f, 0.8f + 0.2f * overCapacity);
        }

        // Update current speed based on congestion
        segment->currentSpeed = segment->freeFlowSpeed * (1.0f - 0.8f * segment->congestionLevel);
    }

    void Simulation::checkAlerts()
    {
        if(!m_alertCallback || !m_roadNetwork)
            return;

        // Check each segment for alert conditions
        std::unordered_set<uint32_t> alertedSegments;

        for(const auto& segmentId : m_roadNetwork->getSegmentIds())
        {
            const auto* segment = m_roadNetwork->getSegment(segmentId);
            if(!segment)
                continue;

            // Check for congestion alert
            if(segment->congestionLevel >= m_alertThresholds[AlertType::CONGESTION])
            {
                std::string msg = "High congestion detected on segment " +
                                  std::to_string(segmentId) + " (" +
                                  std::to_string(int(segment->congestionLevel * 100)) + "%)";
                m_alertCallback(AlertType::CONGESTION, segmentId, msg);
                alertedSegments.insert(segmentId);
            }

            // Check for unusual slowdown (comparing to historical average)
            auto statsIt = m_segmentStats.find(segmentId);
            if(statsIt != m_segmentStats.end())
            {
                const auto& stats = statsIt->second;

                // Calculate historical average speed (from last 10 samples)
                float histAvgSpeed = 0.0f;
                int sampleCount = 0;

                for(size_t i = 0; i < 10 && i < SegmentStatistics::HISTORY_SIZE; ++i)
                {
                    size_t idx = (stats.currentIndex + SegmentStatistics::HISTORY_SIZE - i - 1) %
                                 SegmentStatistics::HISTORY_SIZE;
                    if(stats.timestamps[idx] > 0)
                    {
                        histAvgSpeed += stats.avgSpeed[idx];
                        sampleCount++;
                    }
                }

                if(sampleCount > 0)
                {
                    histAvgSpeed /= sampleCount;

                    // Check if current speed is much lower than historical average
                    if(histAvgSpeed > 0 &&
                       segment->currentSpeed / histAvgSpeed <
                           m_alertThresholds[AlertType::UNUSUAL_SLOWDOWN] &&
                       alertedSegments.find(segmentId) == alertedSegments.end())
                    {

                        std::string msg = "Unusual slowdown on segment " +
                                          std::to_string(segmentId) +
                                          " (Current: " + std::to_string(segment->currentSpeed) +
                                          ", Avg: " + std::to_string(histAvgSpeed) + ")";
                        m_alertCallback(AlertType::UNUSUAL_SLOWDOWN, segmentId, msg);
                        alertedSegments.insert(segmentId);
                    }
                }
            }
        }
    }

} // namespace tfv