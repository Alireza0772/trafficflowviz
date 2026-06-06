#include "core/Simulation.hpp"
#include "core/Configuration.hpp"
#include "utils/LoggingManager.hpp"
#include <algorithm>
#include <data/CSVLoader.hpp>
#include <glm/glm.hpp>
#include <iostream>
#include <random>
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

        // Seed the deterministic RNG stream from the configured master seed so
        // routing choices are reproducible for a given seed (statistical tier).
        m_rng.seed(static_cast<std::mt19937::result_type>(TFV_CONFIG().getMasterSeed()));

        // Clear previous data
        m_world.clear();
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

        // Store vehicles in the World (contiguous, sorted by id for determinism).
        m_world.load(std::move(vehicles));

        // Initialize per-segment occupancy from the loaded vehicles.
        for(const auto& v : m_world.vehicles())
        {
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

        LOG_INFO("Initialized {count} vehicles in the simulation.", PARAM(count, m_world.size()));

        return true;
    }

    void Simulation::update(double dt)
    {
        std::scoped_lock lock(m_mtx);

        // Update time since last statistics update
        m_timeSinceLastUpdate += dt;

        if(m_roadNetwork)
        {
            // Deterministic, ascending-id ordering (no unordered_map iteration).
            auto& vehs = m_world.vehicles();
            const float fdt = static_cast<float>(dt);

            // --- Phase A: sense/decide (read-only over the current frame) ---
            // Compute each vehicle's intended velocity/distance from frame-N state
            // WITHOUT mutating shared world state, so the result is independent of
            // iteration order. This is the substrate the brain step builds on later.
            struct Step
            {
                glm::vec2 vel{0.f, 0.f};
                float speed{0.f};
                float distance{0.f};
                bool valid{false};
            };
            std::vector<Step> steps(vehs.size());
            for(std::size_t i = 0; i < vehs.size(); ++i)
            {
                const Vehicle& v = vehs[i];
                const auto* segment = m_roadNetwork->getSegment(v.segmentId);
                if(!segment)
                    continue;

                // Integrate acceleration. acc is 0 until a brain produces it in
                // Phase 2; this wires the integration path now.
                glm::vec2 newVel = v.vel + v.acc * fdt;
                float speedFactor = 1.0f - segment->congestionLevel * 0.8f;
                float speed = glm::length(newVel) * speedFactor;
                steps[i] = Step{newVel, speed, speed * fdt, true};
            }

            // --- Phase B: act/commit (apply intents to produce frame N+1) ---
            for(std::size_t i = 0; i < vehs.size(); ++i)
            {
                if(!steps[i].valid)
                    continue;
                Vehicle& v = vehs[i];
                auto* segment = m_roadNetwork->getSegment(v.segmentId);
                if(!segment)
                    continue;

                v.vel = steps[i].vel;
                v.position += steps[i].distance / segment->length; // 0..1 along segment

                // If the vehicle passes the end of its segment, hand off to the next.
                if(v.position > 1.f)
                {
                    const auto* fromNode = m_roadNetwork->getNode(segment->toNode);
                    if(fromNode && !fromNode->outgoing.empty())
                    {
                        // Seeded RNG -> deterministic given the master seed.
                        std::uniform_int_distribution<size_t> pick(
                            0, fromNode->outgoing.size() - 1);
                        uint32_t nextSegmentId = fromNode->outgoing[pick(m_rng)];

                        // Maintain per-segment vehicle counts on hand-off.
                        if(segment->vehicleCount > 0)
                            segment->vehicleCount--;
                        if(auto* nextSeg = m_roadNetwork->getSegment(nextSegmentId))
                            nextSeg->vehicleCount++;

                        v.segmentId = nextSegmentId;
                        v.position -= 1.f; // carry over the overshoot
                    }
                    else
                    {
                        v.position -= 1.f; // no outgoing edges: loop within the segment
                    }
                }

                segment->currentSpeed = steps[i].speed;
            }
        }

        // Update segment statistics and congestion levels periodically
        if(m_timeSinceLastUpdate >= m_statUpdateInterval)
        {
            // Count vehicles per segment
            std::unordered_map<uint32_t, int> vehiclesPerSegment;
            std::unordered_map<uint32_t, float> avgSpeedPerSegment;

            for(const auto& v : m_world.vehicles())
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
        return m_world.toMap();
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
        m_world.addVehicle(v);

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
        const Vehicle* vp = m_world.find(id);
        if(vp && m_roadNetwork)
        {
            uint32_t segmentId = vp->segmentId;
            auto* segment = m_roadNetwork->getSegment(segmentId);
            if(segment && segment->vehicleCount > 0)
            {
                segment->vehicleCount--;
                updateCongestion(segmentId);
            }
        }

        m_world.removeVehicle(id);
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