#include "core/Simulation.hpp"
#include "agents/BrainRegistry.hpp"
#include "core/Configuration.hpp"
#include "utils/LoggingManager.hpp"
#include <algorithm>
#include <cmath>
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
        // Load road network (CSV) if one was not supplied via the constructor.
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

        auto vehicles = tfv::loadVehiclesCSV(vehicleInformationPath);
        if(vehicles.empty())
        {
            LOG_ERROR("Failed to load vehicle information from {file}",
                      PARAM(file, vehicleInformationPath.string()));
            return false;
        }

        return initialize(std::move(vehicles));
    }

    bool Simulation::initialize(std::vector<Vehicle> vehicles)
    {
        std::scoped_lock lock(m_mtx);

        if(!m_roadNetwork)
        {
            LOG_ERROR("Simulation::initialize requires a road network");
            return false;
        }

        // Seed the deterministic RNG stream from the configured master seed so
        // routing choices are reproducible for a given seed (statistical tier).
        m_rng.seed(static_cast<std::mt19937::result_type>(TFV_CONFIG().getMasterSeed()));

        // Reset state.
        m_world.clear();
        m_segmentStats.clear();
        m_speedLimits.clear();
        m_lastAction.clear();
        m_forceDecide.clear();
        m_tick = 0;
        m_timeSinceLastUpdate = 0.0;

        // Read model parameters + decision layer from configuration.
        auto& cfg = TFV_CONFIG();
        m_idm.v0_cap = cfg.getFloat("sim.idm.v0_cap", 13.9f);
        m_idm.a_max = cfg.getFloat("sim.idm.a_max", 1.5f);
        m_idm.b_comfort = cfg.getFloat("sim.idm.b_comfort", 2.0f);
        m_idm.b_max = cfg.getFloat("sim.idm.b_max", 6.0f);
        m_idm.s0 = cfg.getFloat("sim.idm.s0", 2.0f);
        m_idm.T = cfg.getFloat("sim.idm.T", 1.5f);
        m_idm.delta = cfg.getFloat("sim.idm.delta", 4.0f);
        m_decisionHz = cfg.getFloat("perf.decision_hz", 10.0f);
        if(m_decisionHz <= 0.0f)
            m_decisionHz = 10.0f;

        m_brain = makeBrain(cfg.getString("sim.default_brain", "rule"), m_idm);
        m_brain->reset(TFV_CONFIG().getMasterSeed());

        // Store vehicles (sorted by id), align velocity to lane direction (avoids a
        // first-tick heading pop), and initialize per-segment occupancy.
        m_world.load(std::move(vehicles));
        for(auto& v : m_world.vehicles())
        {
            auto* segment = m_roadNetwork->getSegment(v.segmentId);
            if(!segment)
                continue;
            float sp = glm::length(v.vel);
            v.vel = segment->dir * sp;
            segment->vehicleCount++;
            updateCongestion(v.segmentId);
        }

        LOG_INFO("Initialized {count} vehicles with brain '{brain}'.",
                 PARAM(count, m_world.size()), PARAM(brain, m_brain->kindName()));
        return true;
    }

    Observation Simulation::buildObservation(const Vehicle& self, long leaderIdx,
                                             const std::vector<Vehicle>& vehs) const
    {
        Observation o{}; // zero-initialized; only longitudinal channels are filled
        const auto* seg = m_roadNetwork ? m_roadNetwork->getSegment(self.segmentId) : nullptr;

        const float vSelf = glm::length(self.vel);
        o[obs_idx::SelfSpeed] = vSelf / OBS_SPEED_SCALE;

        auto it = m_lastAction.find(self.id);
        const float prevAccel = (it != m_lastAction.end()) ? it->second.accel : 0.0f;
        o[obs_idx::SelfAccel] = prevAccel / OBS_ACCEL_SCALE;

        o[obs_idx::PositionAlong] = self.position;
        o[obs_idx::SpeedLimit] = (seg ? seg->speedLimit : 13.9f) / OBS_SPEED_SCALE;
        o[obs_idx::Congestion] = seg ? seg->congestionLevel : 0.0f;

        if(leaderIdx >= 0 && seg)
        {
            const Vehicle& lead = vehs[static_cast<std::size_t>(leaderIdx)];
            float gapM = (lead.position - self.position) * seg->length -
                         0.5f * (self.length + lead.length); // bumper-to-bumper (meters)
            if(gapM < 0.1f)
                gapM = 0.1f;
            const float dv = vSelf - glm::length(lead.vel);
            o[obs_idx::FrontGap] = std::min(gapM / OBS_RANGE_SCALE, 1.0f);
            o[obs_idx::FrontRelSpeed] = dv / OBS_SPEED_SCALE;
            o[obs_idx::FrontHasLeader] = 1.0f;
        }
        else
        {
            o[obs_idx::FrontGap] = 1.0f; // free road
            o[obs_idx::FrontHasLeader] = 0.0f;
        }
        return o;
    }

    void Simulation::update(double dt)
    {
        std::scoped_lock lock(m_mtx);

        // Update time since last statistics update
        m_timeSinceLastUpdate += dt;

        if(m_roadNetwork && m_brain)
        {
            // Deterministic, ascending-id ordering (no unordered_map iteration).
            auto& vehs = m_world.vehicles();
            const float fdt = static_cast<float>(dt);
            const int ticksPerDecision =
                std::max(1, static_cast<int>(std::lround((1.0 / dt) / m_decisionHz)));
            const bool decisionTick = (m_tick % static_cast<uint64_t>(ticksPerDecision)) == 0;

            // ----- Phase A: sense + decide (read-only over the current frame) -----

            // (1) Find each vehicle's same-segment leader deterministically: bucket
            //     indices by segment, sort each bucket by (position asc, id asc); the
            //     leader is the next vehicle strictly ahead. Co-located vehicles
            //     (equal position) are not leaders (avoids a deadlock). Cross-segment
            //     leaders are deferred to the Phase 5 perception system.
            std::unordered_map<uint32_t, std::vector<std::size_t>> bySeg;
            bySeg.reserve(vehs.size());
            for(std::size_t i = 0; i < vehs.size(); ++i)
                bySeg[vehs[i].segmentId].push_back(i);

            std::vector<long> leaderOf(vehs.size(), -1);
            for(auto& [seg, idxs] : bySeg)
            {
                std::sort(idxs.begin(), idxs.end(), [&](std::size_t a, std::size_t b) {
                    if(vehs[a].position != vehs[b].position)
                        return vehs[a].position < vehs[b].position;
                    return vehs[a].id < vehs[b].id;
                });
                for(std::size_t k = 0; k + 1 < idxs.size(); ++k)
                {
                    const std::size_t self = idxs[k];
                    for(std::size_t j = k + 1; j < idxs.size(); ++j)
                    {
                        if(vehs[idxs[j]].position > vehs[self].position)
                        {
                            leaderOf[self] = static_cast<long>(idxs[j]);
                            break;
                        }
                    }
                }
            }

            // (2) Decide actions (batched) for vehicles due this tick; reuse the held
            //     action otherwise. New or just-handed-off vehicles always decide.
            std::vector<std::size_t> deciders;
            deciders.reserve(vehs.size());
            for(std::size_t i = 0; i < vehs.size(); ++i)
            {
                const uint64_t id = vehs[i].id;
                if(decisionTick || m_forceDecide.count(id) || !m_lastAction.count(id))
                    deciders.push_back(i);
            }
            m_forceDecide.clear();

            if(!deciders.empty())
            {
                std::vector<Observation> obs(deciders.size());
                for(std::size_t k = 0; k < deciders.size(); ++k)
                    obs[k] = buildObservation(vehs[deciders[k]], leaderOf[deciders[k]], vehs);

                std::vector<Action> out(deciders.size());
                m_brain->decideBatch(obs.data(), static_cast<int>(deciders.size()), out.data());

                for(std::size_t k = 0; k < deciders.size(); ++k)
                    m_lastAction[vehs[deciders[k]].id] = out[k];
            }

            // (3) Integrate each vehicle's (fresh or held) action into intended motion.
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
                const auto* segment = m_roadNetwork->getSegment(vehs[i].segmentId);
                if(!segment)
                    continue;
                // Every vehicle is guaranteed an entry by the decide step above
                // (deciders include any first-seen id). .at() asserts that invariant
                // and keeps this read-only integrate step free of map mutation.
                const Action& a = m_lastAction.at(vehs[i].id);
                const float accel = std::clamp(a.accel, -m_idm.b_max, m_idm.a_max);
                const float vOld = glm::length(vehs[i].vel);
                const float vNew = std::max(0.0f, vOld + accel * fdt);
                steps[i] = Step{segment->dir * vNew, vNew, vNew * fdt, true};
            }

            // ----- Phase B: act/commit (apply intents to produce frame N+1) -----
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
                        v.position -= 1.f;            // carry over the overshoot
                        m_forceDecide.insert(v.id);   // re-decide for the new segment's v0
                    }
                    else
                    {
                        v.position -= 1.f; // no outgoing edges: loop within the segment
                    }
                }

                segment->currentSpeed = steps[i].speed;
            }

            ++m_tick;
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
        m_lastAction.erase(id);
        m_forceDecide.erase(id);
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