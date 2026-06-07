#include "core/Simulation.hpp"
#include "agents/BrainRegistry.hpp"
#include "agents/Idm.hpp"
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

        // Optional traffic signs (no-op if the file is absent).
        {
            auto signsPath = TFV_CONFIG().getDataDirectory() /
                             TFV_CONFIG().getString("data.signs_file", "roads/signs.csv");
            m_roadNetwork->loadSignsCSV(signsPath);
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
        m_signCleared.clear();
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

        // Signalize intersections and configure the single central light controller.
        m_roadNetwork->buildIntersections(2);
        LightTiming lt;
        lt.greenSec = cfg.getFloat("light.green_sec", 4.0f);
        lt.amberSec = cfg.getFloat("light.amber_sec", 1.0f);
        lt.allRedSec = cfg.getFloat("light.all_red_sec", 0.5f);
        m_lightController = LightController(lt);
        m_lightController.reset(*m_roadNetwork);

        // Perception (Phase 5): params + spatial-grid bounds from the network bbox.
        m_perceptionParams.rangeFront = cfg.getFloat("perception.range_front_m", 60.0f);
        m_perceptionParams.rangeRear = cfg.getFloat("perception.range_rear_m", 30.0f);
        m_perceptionParams.rangeSide = cfg.getFloat("perception.range_side_m", 15.0f);
        m_perceptionParams.sectorFrontDeg = cfg.getFloat("perception.sector_front_deg", 45.0f);
        m_perceptionParams.sectorRearDeg = cfg.getFloat("perception.sector_rear_deg", 45.0f);
        m_crossSegmentLeader = cfg.getInt("perception.cross_segment_leader", 1) != 0;
        {
            glm::vec2 mn(1e30f, 1e30f), mx(-1e30f, -1e30f);
            for(uint32_t sid : m_roadNetwork->getSegmentIds())
                if(const auto* s = m_roadNetwork->getSegment(sid))
                {
                    if(const Node* a = m_roadNetwork->getNode(s->fromNode))
                    {
                        mn = glm::min(mn, a->pos);
                        mx = glm::max(mx, a->pos);
                    }
                    if(const Node* b = m_roadNetwork->getNode(s->toNode))
                    {
                        mn = glm::min(mn, b->pos);
                        mx = glm::max(mx, b->pos);
                    }
                }
            if(mn.x > mx.x) // empty/degenerate network guard
            {
                mn = glm::vec2(0.0f, 0.0f);
                mx = glm::vec2(1.0f, 1.0f);
            }
            const float cell = std::max({m_perceptionParams.rangeFront,
                                         m_perceptionParams.rangeRear, m_perceptionParams.rangeSide});
            m_gridMin = mn - glm::vec2(cell, cell); // cached for inspect()'s local grid
            m_gridMax = mx + glm::vec2(cell, cell);
            m_gridCell = cell;
            m_grid.setBounds(m_gridMin, m_gridMax, m_gridCell);
        }

        // MOBIL lane-change (Phase 6).
        m_laneWidth = cfg.getFloat("sim.lane_width_m", 3.5f);
        m_mobil.enabled = cfg.getInt("sim.mobil.enabled", 1) != 0;
        m_mobil.bSafe = cfg.getFloat("sim.mobil.b_safe", 4.0f);
        m_mobil.politeness = cfg.getFloat("sim.mobil.politeness", 0.2f);
        m_mobil.threshold = cfg.getFloat("sim.mobil.threshold", 0.2f);
        m_mobil.biasRight = cfg.getFloat("sim.mobil.bias_right", 0.1f);
        m_mobil.cooldownSec = cfg.getFloat("sim.mobil.cooldown_sec", 2.0f);
        if(m_mobil.bSafe > m_idm.b_max) // never authorize a change the integrator must hard-brake
            m_mobil.bSafe = m_idm.b_max;
        m_laneChangeCooldown.clear();

        // Action validator (Phase 7): clamps/scrubs brain output; default on.
        m_validator.setEnabled(cfg.getInt("validator.enabled", 1) != 0);
        m_violations.clear();

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
            v.worldPos = segWorldPos(*segment, v.position, v.laneIndex);
            {
                const glm::vec2 t = segment->geometry().tangentAt(v.position * segment->length);
                v.heading = std::atan2(t.y, t.x);
            }
            segment->vehicleCount++;
            updateCongestion(v.segmentId);

            // Routing-by-intent: compute the deterministic BFS path toward the
            // destination node (if any); otherwise the vehicle wanders (outgoing[0]).
            if(v.destNode != 0)
            {
                v.route = m_roadNetwork->route(segment->toNode, v.destNode);
                v.routeIdx = 0;
            }
        }

        LOG_INFO("Initialized {count} vehicles with brain '{brain}'.",
                 PARAM(count, m_world.size()), PARAM(brain, m_brain->kindName()));
        return true;
    }

    Observation Simulation::buildObservation(const Vehicle& self, long leaderIdx,
                                             const std::vector<Vehicle>& vehs,
                                             const std::array<SensedNeighbor, 4>& sectors,
                                             float crossGapNorm, float crossRelSpeed, bool hasCross,
                                             double dt) const
    {
        Observation o{}; // zero-initialized; only longitudinal channels are filled
        const auto* seg = m_roadNetwork ? m_roadNetwork->getSegment(self.segmentId) : nullptr;

        const float vSelf = glm::length(self.vel);
        o[obs_idx::SelfSpeed] = vSelf / OBS_SPEED_SCALE;

        auto it = m_lastAction.find(self.id);
        const float prevAccel = (it != m_lastAction.end()) ? it->second.accel : 0.0f;
        o[obs_idx::SelfAccel] = prevAccel / OBS_ACCEL_SCALE;

        o[obs_idx::PositionAlong] = self.position;
        o[obs_idx::Congestion] = seg ? seg->congestionLevel : 0.0f;

        // Lane channels (lane-keeping; lane geometry is forward-compatible).
        const int laneCount = seg ? std::max(1, seg->lanes) : 1;
        o[obs_idx::LaneCount] = static_cast<float>(laneCount) / 8.0f;
        o[obs_idx::LaneFraction] =
            (laneCount > 1) ? static_cast<float>(self.laneIndex) / static_cast<float>(laneCount - 1)
                            : 0.0f;
        if(seg)
            o[obs_idx::DistToNextIntersection] =
                std::min(((1.0f - self.position) * seg->length) / OBS_RANGE_SCALE, 1.0f);

        // Sign-aware effective speed limit (SPEED_LIMIT signs already passed apply).
        float effLimit = seg ? seg->speedLimit : 13.9f;
        if(seg)
            for(uint32_t sid : seg->signIds)
            {
                const Sign* s = m_roadNetwork->getSign(sid);
                if(s && s->type == SignType::SPEED_LIMIT && s->value > 0.0f &&
                   s->pos <= self.position + 1e-3f)
                    effLimit = std::min(effLimit, s->value);
            }
        if(self.maxSpeed > 0.0f) // per-vehicle desired-speed cap (e.g. a slow truck); 0 = no cap
            effLimit = std::min(effLimit, self.maxSpeed);
        o[obs_idx::SpeedLimit] = effLimit / OBS_SPEED_SCALE;

        const Sign* stopSign = nearestStopAhead(self);
        o[obs_idx::NearestSignType] =
            stopSign ? (stopSign->type == SignType::STOP ? 1.0f : 0.66f) : 0.0f;

        // Front constraint = the MORE-restrictive of {real same-segment leader,
        // governing stop/yield sign} — written into one front-sector slot, never both.
        bool hasFront = false;
        float frontGapNorm = 1.0f; // free road
        float frontRelSpeed = 0.0f;
        if(leaderIdx >= 0 && seg)
        {
            const Vehicle& lead = vehs[static_cast<std::size_t>(leaderIdx)];
            float gapM = (lead.position - self.position) * seg->length -
                         0.5f * (self.length + lead.length); // bumper-to-bumper (meters)
            if(gapM < 0.1f)
                gapM = 0.1f;
            frontGapNorm = std::min(gapM / OBS_RANGE_SCALE, 1.0f);
            frontRelSpeed = (vSelf - glm::length(lead.vel)) / OBS_SPEED_SCALE;
            hasFront = true;
        }
        else if(hasCross && seg)
        {
            // No same-segment leader, but a car sits just past the node on our path
            // (cross-segment lookahead). Path-relative, never a world-space cone car.
            frontGapNorm = crossGapNorm;
            frontRelSpeed = crossRelSpeed;
            hasFront = true;
        }
        if(stopSign && seg)
        {
            auto cl = m_signCleared.find(self.id);
            const bool cleared = (cl != m_signCleared.end() && cl->second == stopSign->id);
            if(!cleared)
            {
                float gapM = (stopSign->pos - self.position) * seg->length - 0.5f * self.length;
                if(gapM < 0.1f)
                    gapM = 0.1f;
                const float signGapNorm = std::min(gapM / OBS_RANGE_SCALE, 1.0f);
                if(!hasFront || signGapNorm < frontGapNorm)
                {
                    frontGapNorm = signGapNorm;
                    frontRelSpeed = vSelf / OBS_SPEED_SCALE; // stationary stop line
                    hasFront = true;
                }
            }
        }
        // Traffic-light obedience (Phase 4): a red/amber light at the segment end is a
        // conditional virtual stop-leader, merged more-restrictively into the front
        // sector. On green there is no constraint and the vehicle proceeds.
        const LightColor lc =
            m_roadNetwork ? m_roadNetwork->approachColor(self.segmentId) : LightColor::Green;
        o[obs_idx::SignalPhase] =
            (lc == LightColor::Red) ? 1.0f : (lc == LightColor::Amber ? 0.5f : 0.0f);
        if(seg && (lc == LightColor::Red || lc == LightColor::Amber))
        {
            float gapM = (1.0f - self.position) * seg->length - 0.5f * self.length;
            if(gapM < 0.1f)
                gapM = 0.1f;
            const float lightGapNorm = std::min(gapM / OBS_RANGE_SCALE, 1.0f);
            if(!hasFront || lightGapNorm < frontGapNorm)
            {
                frontGapNorm = lightGapNorm;
                frontRelSpeed = vSelf / OBS_SPEED_SCALE; // stationary stop line at the node
                hasFront = true;
            }
        }

        o[obs_idx::FrontGap] = frontGapNorm;
        o[obs_idx::FrontRelSpeed] = frontRelSpeed;
        o[obs_idx::FrontHasLeader] = hasFront ? 1.0f : 0.0f;

        // Signal time-to-change (idx 9): seconds remaining in the current light phase.
        if(seg && m_roadNetwork)
        {
            const auto& xs = m_roadNetwork->intersections();
            auto xit = xs.find(seg->toNode);
            if(xit != xs.end())
            {
                const auto& X = xit->second;
                const auto& t = m_lightController.timing();
                const float colorSec = (X.activeColor == LightColor::Green)  ? t.greenSec
                                       : (X.activeColor == LightColor::Amber) ? t.amberSec
                                                                              : t.allRedSec;
                // Quantize the phase length the SAME way LightController does, so the
                // reported time-to-change matches the exact tick at which it flips.
                const long colorTicks = std::max(1L, std::lround(colorSec / dt));
                float remaining =
                    static_cast<float>((colorTicks - static_cast<long>(X.ticksInPhase)) * dt);
                if(remaining < 0.0f)
                    remaining = 0.0f;
                o[obs_idx::SignalTimeToChange] = std::min(remaining / OBS_TIME_SCALE, 1.0f);
            }
        }

        // World-space sector neighbours (idx 14-22): rear / left / right.
        auto fillSector = [&](int base, const SensedNeighbor& n) {
            if(!n.valid)
                return;
            o[base + 0] = std::min(n.relDist / OBS_RANGE_SCALE, 1.0f);
            o[base + 1] = std::clamp(n.relSpeed / OBS_SPEED_SCALE, -1.0f, 1.0f);
            o[base + 2] = static_cast<float>(n.lightBits & 0x1F) / 31.0f;
        };
        fillSector(obs_idx::RearRelDist, sectors[static_cast<int>(Sector::Rear)]);
        fillSector(obs_idx::LeftRelDist, sectors[static_cast<int>(Sector::Left)]);
        fillSector(obs_idx::RightRelDist, sectors[static_cast<int>(Sector::Right)]);

        return o;
    }

    void Simulation::evaluateLaneChanges(
        const std::vector<Vehicle>& vehs,
        const std::unordered_map<uint32_t, std::vector<std::size_t>>& bySeg,
        const std::vector<long>& leaderOf, std::vector<int>& desiredLaneChange)
    {
        if(!m_mobil.enabled || !m_roadNetwork)
            return;

        // Per-vehicle desired speed (segment limit, idm cap, optional per-vehicle cap).
        auto v0Of = [&](const Vehicle& x, const RoadSegment& seg) -> float {
            float lim = std::min(seg.speedLimit, m_idm.v0_cap);
            if(x.maxSpeed > 0.0f)
                lim = std::min(lim, x.maxSpeed);
            return std::max(0.1f, lim);
        };
        // IDM accel of follower f if it were following leader leadIdx (<0 = free road).
        auto accelWith = [&](const Vehicle& f, long leadIdx, const RoadSegment& seg) -> float {
            const float v = glm::length(f.vel);
            const float v0 = v0Of(f, seg);
            if(leadIdx < 0)
                return idmAccel(v, v0, 1e9f, 0.0f, false, m_idm);
            const Vehicle& l = vehs[static_cast<std::size_t>(leadIdx)];
            float gapM = (l.position - f.position) * seg.length - 0.5f * (f.length + l.length);
            if(gapM < 0.1f)
                gapM = 0.1f;
            return idmAccel(v, v0, gapM, v - glm::length(l.vel), true, m_idm);
        };

        for(const auto& [segId, idxs] : bySeg)
        {
            const auto* seg = m_roadNetwork->getSegment(segId);
            if(!seg)
                continue;
            const int laneCount = std::max(1, seg->lanes);
            if(laneCount < 2) // single-lane: no lane change possible (load-bearing for the digest)
                continue;

            for(std::size_t si : idxs)
            {
                const Vehicle& self = vehs[si];
                auto cd = m_laneChangeCooldown.find(self.id);
                if(cd != m_laneChangeCooldown.end() && m_tick < cd->second)
                    continue; // cooling down from a recent change
                if((1.0f - self.position) * seg->length <= m_perceptionParams.rangeFront)
                    continue; // never change lanes while approaching a node/sign/light

                const long curLeader = leaderOf[si];
                const float aCur = accelWith(self, curLeader, *seg);

                // Current-lane follower (nearest behind in self's lane).
                long curFollower = -1;
                float curFollowerPos = -2e9f;
                for(std::size_t j : idxs)
                {
                    if(j == si)
                        continue;
                    const Vehicle& o = vehs[j];
                    if(o.laneIndex == self.laneIndex && o.position < self.position &&
                       o.position > curFollowerPos)
                    {
                        curFollowerPos = o.position;
                        curFollower = static_cast<long>(j);
                    }
                }

                int bestDir = 0;
                float bestIncentive = m_mobil.threshold; // must strictly beat the threshold
                for(int dir = -1; dir <= 1; dir += 2)
                {
                    const int L = static_cast<int>(self.laneIndex) + dir;
                    if(L < 0 || L >= laneCount)
                        continue;

                    long tLeader = -1, tFollower = -1;
                    float tLeaderPos = 2e9f, tFollowerPos = -2e9f;
                    for(std::size_t j : idxs)
                    {
                        if(j == si)
                            continue;
                        const Vehicle& o = vehs[j];
                        if(static_cast<int>(o.laneIndex) != L)
                            continue;
                        if(o.position > self.position)
                        {
                            if(o.position < tLeaderPos)
                            {
                                tLeaderPos = o.position;
                                tLeader = static_cast<long>(j);
                            }
                        }
                        else if(o.position > tFollowerPos)
                        {
                            tFollowerPos = o.position;
                            tFollower = static_cast<long>(j);
                        }
                    }

                    const float aNew = accelWith(self, tLeader, *seg); // ego in the target lane
                    if(aNew < -m_mobil.bSafe) // hard floor: don't dive behind a leader we'd slam into
                        continue;

                    // New target-lane follower, before/after ego inserts ahead of it.
                    float aFolNew = 0.0f, aFolOld = 0.0f;
                    if(tFollower >= 0)
                    {
                        aFolOld = accelWith(vehs[tFollower], tLeader, *seg);
                        aFolNew = accelWith(vehs[tFollower], static_cast<long>(si), *seg);
                        if(aFolNew < -m_mobil.bSafe) // MOBIL safety: don't force an emergency brake
                            continue;
                    }
                    // Ego's current follower, before/after ego leaves its lane.
                    float aOldFolBefore = 0.0f, aOldFolAfter = 0.0f;
                    if(curFollower >= 0)
                    {
                        aOldFolBefore = accelWith(vehs[curFollower], static_cast<long>(si), *seg);
                        aOldFolAfter = accelWith(vehs[curFollower], curLeader, *seg);
                    }

                    // Asymmetric (keep-right) MOBIL: move LEFT only to gain your OWN
                    // speed (passing lane), and RIGHT out of courtesy + keep-right.
                    // Applying the old-follower courtesy term to LEFT moves would make a
                    // slow car perversely yield into the very lane the overtaker wants.
                    float incentive = (aNew - aCur) + m_mobil.politeness * (aFolNew - aFolOld);
                    if(dir < 0)
                        incentive += m_mobil.politeness * (aOldFolAfter - aOldFolBefore) +
                                     m_mobil.biasRight; // keep-right + courtesy yielding right
                    else
                        incentive -= m_mobil.biasRight; // discourage entering the passing lane

                    if(incentive > bestIncentive)
                    {
                        bestIncentive = incentive;
                        bestDir = dir;
                    }
                }

                desiredLaneChange[si] = bestDir;
                if(bestDir != 0)
                {
                    auto la = m_lastAction.find(self.id);
                    if(la != m_lastAction.end())
                        la->second.laneChange = static_cast<int8_t>(bestDir); // future-brain seam
                }
            }
        }
    }

    uint32_t Simulation::nextSegmentForLookahead(const Vehicle& v, const RoadSegment& seg) const
    {
        const Node* toNode = m_roadNetwork ? m_roadNetwork->getNode(seg.toNode) : nullptr;
        if(!toNode || toNode->outgoing.empty())
            return UINT32_MAX;
        if(v.routeIdx < v.route.size())
        {
            const uint32_t want = v.route[v.routeIdx];
            if(std::find(toNode->outgoing.begin(), toNode->outgoing.end(), want) !=
               toNode->outgoing.end())
                return want;
        }
        return toNode->outgoing[0];
    }

    glm::vec2 Simulation::segWorldPos(const RoadSegment& seg, float position, uint8_t laneIndex) const
    {
        glm::vec2 base(0.0f, 0.0f);
        if(m_roadNetwork)
            if(const Node* n = m_roadNetwork->getNode(seg.fromNode))
                base = n->pos;
        // Route through the SegmentGeometry seam (straight today; curves in Phase E).
        const SegmentGeometry geom = seg.geometry();
        const float arc = position * seg.length;
        glm::vec2 p = base + geom.offsetAt(arc);
        // Lateral shift = direction-separating median (Phase B) + lane offset. One-way
        // single-lane roads have medianOffset==0 and laneCount==1 => lat==0 => byte-identical.
        float lat = seg.medianOffset;
        const int laneCount = std::max(1, seg.lanes);
        if(laneCount > 1)
            lat += (static_cast<float>(laneIndex) - (laneCount - 1) * 0.5f) * m_laneWidth;
        if(lat != 0.0f)
            p += geom.normalAt(arc) * lat; // matches RoadRenderer (SDL y-down)
        return p;
    }

    const Sign* Simulation::nearestStopAhead(const Vehicle& v) const
    {
        if(!m_roadNetwork)
            return nullptr;
        const auto* seg = m_roadNetwork->getSegment(v.segmentId);
        if(!seg)
            return nullptr;
        const Sign* best = nullptr;
        float bestPos = 2.0f;
        for(uint32_t sid : seg->signIds)
        {
            const Sign* s = m_roadNetwork->getSign(sid);
            if(!s)
                continue;
            if((s->type == SignType::STOP || s->type == SignType::YIELD) && s->pos > v.position &&
               s->pos < bestPos)
            {
                bestPos = s->pos;
                best = s;
            }
        }
        return best;
    }

    void Simulation::update(double dt)
    {
        std::scoped_lock lock(m_mtx);

        m_lastDt = dt; // remembered for inspect()'s time-to-change channel (not used by the step)

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
                        // Lane-aware: a vehicle follows the next car ahead IN ITS OWN LANE.
                        // Single-lane (all laneIndex 0) reduces to the original behaviour.
                        if(vehs[idxs[j]].laneIndex == vehs[self].laneIndex &&
                           vehs[idxs[j]].position > vehs[self].position)
                        {
                            leaderOf[self] = static_cast<long>(idxs[j]);
                            break;
                        }
                    }
                }
            }

            // (1b) Perception substrate: rebuild the spatial grid from frozen frame-N
            //      world positions, and compute the cross-segment leader override (the
            //      car just past the node on this vehicle's path) for vehicles with no
            //      same-segment leader. bySeg buckets are (position asc, id asc) sorted,
            //      so front() is the nearest car to the downstream node.
            m_grid.rebuild(vehs);
            std::vector<float> crossGap(vehs.size(), 1.0f);
            std::vector<float> crossRel(vehs.size(), 0.0f);
            std::vector<char> hasCross(vehs.size(), 0);
            if(m_crossSegmentLeader)
            {
                for(std::size_t i = 0; i < vehs.size(); ++i)
                {
                    if(leaderOf[i] >= 0)
                        continue;
                    const Vehicle& v = vehs[i];
                    const auto* seg = m_roadNetwork->getSegment(v.segmentId);
                    if(!seg)
                        continue;
                    const float remM = (1.0f - v.position) * seg->length;
                    if(remM > m_perceptionParams.rangeFront)
                        continue; // not near the node yet
                    const uint32_t nextId = nextSegmentForLookahead(v, *seg);
                    if(nextId == UINT32_MAX)
                        continue;
                    const auto* nextSeg = m_roadNetwork->getSegment(nextId);
                    if(!nextSeg)
                        continue;
                    auto bit = bySeg.find(nextId);
                    if(bit == bySeg.end() || bit->second.empty())
                        continue;
                    // Lane-aware: the ego enters the lane it will be clamped into on the
                    // downstream segment (mirrors the hand-off clamp in Phase B). Scan that
                    // lane's nearest (min-position) car instead of the any-lane front(), so a
                    // car never brakes for a downstream vehicle in a lane it won't occupy.
                    // Buckets are (position asc, id asc) sorted, so the first match is nearest.
                    // Single-lane downstream: enterLane==0 and all cars are lane 0 -> identical
                    // to front() (the golden fork's downstream is single-lane: kGolden stays).
                    const int enterLane =
                        std::min<int>(v.laneIndex, std::max(0, nextSeg->lanes - 1));
                    long leadIdx = -1;
                    for(std::size_t j : bit->second)
                        if(static_cast<int>(vehs[j].laneIndex) == enterLane)
                        {
                            leadIdx = static_cast<long>(j);
                            break;
                        }
                    if(leadIdx < 0)
                        continue; // no car in the lane we will enter -> free road downstream
                    const Vehicle& lead = vehs[static_cast<std::size_t>(leadIdx)];
                    float gapM =
                        remM + lead.position * nextSeg->length - 0.5f * (v.length + lead.length);
                    if(gapM < 0.1f)
                        gapM = 0.1f;
                    crossGap[i] = std::min(gapM / OBS_RANGE_SCALE, 1.0f);
                    crossRel[i] = (glm::length(v.vel) - glm::length(lead.vel)) / OBS_SPEED_SCALE;
                    hasCross[i] = 1;
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
                {
                    const std::size_t i = deciders[k];
                    const std::array<SensedNeighbor, 4> sectors =
                        sense(i, vehs, m_grid, m_perceptionParams);
                    obs[k] = buildObservation(vehs[i], leaderOf[i], vehs, sectors, crossGap[i],
                                              crossRel[i], hasCross[i] != 0, dt);
                }

                std::vector<Action> out(deciders.size());
                std::vector<uint64_t> ids(deciders.size()); // per-vehicle ids for id-aware brains
                for(std::size_t k = 0; k < deciders.size(); ++k)
                    ids[k] = vehs[deciders[k]].id;
                m_brain->decideBatchIds(obs.data(), ids.data(),
                                        static_cast<int>(deciders.size()), out.data());

                for(std::size_t k = 0; k < deciders.size(); ++k)
                    m_lastAction[vehs[deciders[k]].id] = out[k];

                // (2a) Validate each freshly produced action (clamp/scrub; tally per-agent
                //      violations). A byte-identical no-op on the rule brain's output.
                for(std::size_t k = 0; k < deciders.size(); ++k)
                {
                    const std::size_t i = deciders[k];
                    const auto* seg = m_roadNetwork->getSegment(vehs[i].segmentId);
                    const int laneCount = seg ? std::max(1, seg->lanes) : 1;
                    const uint32_t nv = m_validator.validate(m_lastAction[vehs[i].id], laneCount,
                                                             vehs[i].laneIndex, m_idm);
                    if(nv)
                        m_violations[vehs[i].id] += nv;
                }
            }

            // (2b) Source the desired lane change: a brain that drives lane changes
            //      provides its (validated) Action.laneChange; otherwise the built-in
            //      MOBIL evaluator decides. Phase B0 is the single conflict-free committer.
            std::vector<int> desiredLaneChange(vehs.size(), 0);
            if(m_brain->drivesLaneChange())
            {
                for(std::size_t i = 0; i < vehs.size(); ++i)
                {
                    auto la = m_lastAction.find(vehs[i].id);
                    if(la != m_lastAction.end())
                        desiredLaneChange[i] = la->second.laneChange;
                }
            }
            else
            {
                evaluateLaneChanges(vehs, bySeg, leaderOf, desiredLaneChange);
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
                float vNew = std::max(0.0f, vOld + accel * fdt);
                // Physical speed governor (independent of the brain): never exceed the
                // effective desired speed. Identity on the IDM rule brain (which already
                // self-limits below v0), so it cannot move the rule digests; it makes the
                // NN safety bound a guarantee rather than a seed coincidence.
                float vCap = std::min(segment->speedLimit, m_idm.v0_cap);
                if(vehs[i].maxSpeed > 0.0f)
                    vCap = std::min(vCap, vehs[i].maxSpeed);
                if(vNew > vCap)
                    vNew = vCap;
                steps[i] = Step{segment->dir * vNew, vNew, vNew * fdt, true};
            }

            // ----- Phase B0: commit lane changes (frame-N positions intact; ascending id) -----
            // Run whenever lane changes are possible: MOBIL (rule path) OR a brain that
            // drives its own lane changes — so a brain's lateral channel isn't silently
            // disabled by the rule-path MOBIL toggle. A no-op when desiredLaneChange is all 0.
            if(m_mobil.enabled || m_brain->drivesLaneChange())
            {
                const uint64_t cooldownTicks =
                    static_cast<uint64_t>(std::max(1.0, m_mobil.cooldownSec / dt));
                for(std::size_t i = 0; i < vehs.size(); ++i)
                {
                    const int dir = desiredLaneChange[i];
                    if(dir == 0)
                        continue;
                    Vehicle& v = vehs[i];
                    const auto* seg = m_roadNetwork->getSegment(v.segmentId);
                    if(!seg)
                        continue;
                    const int laneCount = std::max(1, seg->lanes);
                    const int L = static_cast<int>(v.laneIndex) + dir;
                    if(L < 0 || L >= laneCount)
                        continue;
                    // Re-check against CURRENT assignments (lower-id vehicles may have moved
                    // into L this tick): require >= s0 clearance to every car in lane L, so
                    // two vehicles can never claim the same gap. Lowest id wins.
                    bool ok = true;
                    for(std::size_t j = 0; j < vehs.size(); ++j)
                    {
                        if(j == i)
                            continue;
                        const Vehicle& o = vehs[j];
                        if(o.segmentId != v.segmentId || static_cast<int>(o.laneIndex) != L)
                            continue;
                        const float gapM = std::fabs(o.position - v.position) * seg->length -
                                           0.5f * (v.length + o.length);
                        if(gapM < m_idm.s0)
                        {
                            ok = false;
                            break;
                        }
                    }
                    if(!ok)
                        continue;
                    v.prevLaneIndex = v.laneIndex;
                    v.laneIndex = static_cast<uint8_t>(L);
                    v.laneChangeTick = m_tick;
                    m_laneChangeCooldown[v.id] = m_tick + cooldownTicks;
                    auto la = m_lastAction.find(v.id);
                    if(la != m_lastAction.end())
                        la->second.laneChange = 0; // consumed
                }
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

                // Stop/yield clearing: once slow near an uncleared sign on this segment,
                // mark it cleared so the vehicle proceeds next decision (re-accelerate).
                if(const Sign* sgn = nearestStopAhead(v))
                {
                    auto cl = m_signCleared.find(v.id);
                    const bool cleared = (cl != m_signCleared.end() && cl->second == sgn->id);
                    if(!cleared)
                    {
                        // Hard stop line: an uncleared STOP must not be overshot (bounded
                        // braking cannot always halt in time) — clamp to the stop line.
                        if(sgn->type == SignType::STOP && v.position >= sgn->pos)
                        {
                            v.position = sgn->pos;
                            v.vel = glm::vec2(0.0f, 0.0f);
                        }
                        // Clear once stopped/slow at the line. Measure the gap the SAME way
                        // as the IDM virtual leader (bumper -> stop line) so the threshold
                        // actually covers the equilibrium stopping distance (~s0); otherwise
                        // the car parks just outside the gate and deadlocks forever.
                        const float gapM = (sgn->pos - v.position) * segment->length - 0.5f * v.length;
                        const float releaseSpeed = (sgn->type == SignType::STOP) ? 0.5f : 4.0f;
                        if(glm::length(v.vel) < releaseSpeed && gapM < (m_idm.s0 + 1.0f))
                        {
                            m_signCleared[v.id] = sgn->id;
                            m_forceDecide.insert(v.id);
                        }
                    }
                }

                // Red-light hard stop-line: bounded IDM braking cannot always halt in
                // time, so never let a vehicle cross a node while its approach is RED.
                // (Amber still permits clearing the dilemma zone.)
                if(v.position > 1.0f &&
                   m_roadNetwork->approachColor(v.segmentId) == LightColor::Red)
                {
                    v.position = 1.0f - 1e-4f;
                    v.vel = glm::vec2(0.0f, 0.0f);
                }

                // If the vehicle passes the end of its segment, hand off to the next.
                if(v.position > 1.f)
                {
                    const auto* fromNode = m_roadNetwork->getNode(segment->toNode);
                    if(fromNode && !fromNode->outgoing.empty())
                    {
                        // Deterministic route-following (no RNG): take the route's next
                        // segment if it is a valid outgoing here, else fall back to outgoing[0].
                        uint32_t nextSegmentId = fromNode->outgoing[0];
                        if(v.routeIdx < v.route.size())
                        {
                            const uint32_t want = v.route[v.routeIdx];
                            if(std::find(fromNode->outgoing.begin(), fromNode->outgoing.end(),
                                         want) != fromNode->outgoing.end())
                            {
                                nextSegmentId = want;
                                ++v.routeIdx;
                            }
                        }

                        // Commit the hand-off only if the next segment really exists;
                        // otherwise leave the vehicle in a consistent state (loop).
                        if(auto* nextSeg = m_roadNetwork->getSegment(nextSegmentId))
                        {
                            if(segment->vehicleCount > 0)
                                segment->vehicleCount--;
                            nextSeg->vehicleCount++;
                            // Lane-keeping: clamp lane index to the new segment's lane count.
                            v.laneIndex = static_cast<uint8_t>(
                                std::min<int>(v.laneIndex, std::max(0, nextSeg->lanes - 1)));
                            // Keep the render/observation lane state consistent across the
                            // hand-off so a clamp can't assert a phantom turn signal.
                            v.prevLaneIndex = v.laneIndex;
                            v.segmentId = nextSegmentId;
                            v.position -= 1.f;          // carry over the overshoot
                            m_signCleared.erase(v.id);  // re-evaluate signs on the new segment
                            m_forceDecide.insert(v.id); // re-decide for the new segment's v0
                        }
                        else
                        {
                            v.position -= 1.f; // next segment missing (bad data): loop in place
                        }
                    }
                    else
                    {
                        v.position -= 1.f; // no outgoing edges: loop within the segment
                    }
                }

                segment->currentSpeed = steps[i].speed;

                // Observable state for perception/rendering: light bits (only BRAKE is
                // emitted in Phase 5) and the authoritative world pose on the CURRENT
                // (possibly newly handed-off) segment.
                {
                    auto la = m_lastAction.find(v.id);
                    uint8_t bits =
                        (la != m_lastAction.end()) ? (la->second.lightCmd & light::BRAKE) : 0;
                    // Turn signal during the lane-change render window.
                    const uint64_t signalTicks = static_cast<uint64_t>(std::max(1.0, 0.8 / dt));
                    if(v.prevLaneIndex != v.laneIndex && (m_tick - v.laneChangeTick) < signalTicks)
                        bits |= (v.laneIndex < v.prevLaneIndex) ? light::LEFT : light::RIGHT;
                    v.lightBits = bits;
                }
                if(auto* cur = m_roadNetwork->getSegment(v.segmentId))
                {
                    v.worldPos = segWorldPos(*cur, v.position, v.laneIndex);
                    const glm::vec2 t = cur->geometry().tangentAt(v.position * cur->length);
                    v.heading = std::atan2(t.y, t.x);
                }
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

        // Advance the single central traffic-light controller for the next tick
        // (deterministic; vehicles this tick read the start-of-tick signal state).
        if(m_roadNetwork)
            m_lightController.step(*m_roadNetwork, dt);
    }

    VehicleMap Simulation::snapshot() const
    {
        std::scoped_lock lock(m_mtx);
        return m_world.toMap();
    }

    std::size_t Simulation::vehicleCount() const
    {
        std::scoped_lock lock(m_mtx);
        return m_world.size();
    }

    uint32_t Simulation::violationCount(uint64_t id) const
    {
        std::scoped_lock lock(m_mtx);
        auto it = m_violations.find(id);
        return (it != m_violations.end()) ? it->second : 0u;
    }

    uint64_t Simulation::totalViolations() const
    {
        std::scoped_lock lock(m_mtx);
        uint64_t sum = 0;
        for(const auto& [id, v] : m_violations)
        {
            (void)id;
            sum += v;
        }
        return sum;
    }

    std::string Simulation::brainKind() const
    {
        std::scoped_lock lock(m_mtx);
        return m_brain ? std::string(m_brain->kindName()) : "none";
    }

    std::string Simulation::brainWeightsHash() const
    {
        std::scoped_lock lock(m_mtx);
        return m_brain ? m_brain->weightsHash() : "n/a";
    }

    std::unordered_map<uint64_t, Action> Simulation::lastActions() const
    {
        std::scoped_lock lock(m_mtx);
        return m_lastAction;
    }

    VehicleInspection Simulation::inspect(uint64_t id) const
    {
        std::scoped_lock lock(m_mtx);
        VehicleInspection r;
        if(!m_roadNetwork)
            return r;
        const auto& vehs = m_world.vehicles(); // ascending-id

        long selfIdx = -1;
        for(std::size_t i = 0; i < vehs.size(); ++i)
            if(vehs[i].id == id)
            {
                selfIdx = static_cast<long>(i);
                break;
            }
        if(selfIdx < 0)
            return r;
        const std::size_t si = static_cast<std::size_t>(selfIdx);

        // Replicate Phase A locally (read-only): same-lane leader + cross-segment override.
        std::unordered_map<uint32_t, std::vector<std::size_t>> bySeg;
        for(std::size_t i = 0; i < vehs.size(); ++i)
            bySeg[vehs[i].segmentId].push_back(i);
        for(auto& [seg, idxs] : bySeg)
        {
            (void)seg;
            std::sort(idxs.begin(), idxs.end(), [&](std::size_t a, std::size_t b) {
                if(vehs[a].position != vehs[b].position)
                    return vehs[a].position < vehs[b].position;
                return vehs[a].id < vehs[b].id;
            });
        }
        long leaderIdx = -1;
        {
            const auto& idxs = bySeg[vehs[si].segmentId];
            std::size_t k = 0;
            for(; k < idxs.size(); ++k)
                if(idxs[k] == si)
                    break;
            for(std::size_t j = k + 1; j < idxs.size(); ++j)
                if(vehs[idxs[j]].laneIndex == vehs[si].laneIndex &&
                   vehs[idxs[j]].position > vehs[si].position)
                {
                    leaderIdx = static_cast<long>(idxs[j]);
                    break;
                }
        }
        float crossGap = 1.0f, crossRel = 0.0f;
        bool hasCross = false;
        if(m_crossSegmentLeader && leaderIdx < 0)
        {
            const Vehicle& v = vehs[si];
            const auto* seg = m_roadNetwork->getSegment(v.segmentId);
            if(seg)
            {
                const float remM = (1.0f - v.position) * seg->length;
                if(remM <= m_perceptionParams.rangeFront)
                {
                    const uint32_t nextId = nextSegmentForLookahead(v, *seg);
                    const auto* nextSeg =
                        (nextId != UINT32_MAX) ? m_roadNetwork->getSegment(nextId) : nullptr;
                    auto bit = bySeg.find(nextId);
                    if(nextSeg && bit != bySeg.end() && !bit->second.empty())
                    {
                        // Lane-aware: scan the lane the ego will enter downstream (mirrors the
                        // update() cross-seg block and the Phase-B hand-off clamp). MUST stay
                        // byte-identical to update() or inspect() reports a phantom leader.
                        const int enterLane =
                            std::min<int>(v.laneIndex, std::max(0, nextSeg->lanes - 1));
                        long leadIdx = -1;
                        for(std::size_t j : bit->second)
                            if(static_cast<int>(vehs[j].laneIndex) == enterLane)
                            {
                                leadIdx = static_cast<long>(j);
                                break;
                            }
                        if(leadIdx >= 0)
                        {
                            const Vehicle& lead = vehs[static_cast<std::size_t>(leadIdx)];
                            float gapM = remM + lead.position * nextSeg->length -
                                         0.5f * (v.length + lead.length);
                            if(gapM < 0.1f)
                                gapM = 0.1f;
                            crossGap = std::min(gapM / OBS_RANGE_SCALE, 1.0f);
                            crossRel =
                                (glm::length(v.vel) - glm::length(lead.vel)) / OBS_SPEED_SCALE;
                            hasCross = true;
                        }
                    }
                }
            }
        }

        UniformGrid grid; // local (inspect is const; never touch m_grid)
        grid.setBounds(m_gridMin, m_gridMax, m_gridCell);
        grid.rebuild(vehs);
        r.sectors = sense(si, vehs, grid, m_perceptionParams);
        r.obs = buildObservation(vehs[si], leaderIdx, vehs, r.sectors, crossGap, crossRel, hasCross,
                                 m_lastDt);
        auto la = m_lastAction.find(id);
        if(la != m_lastAction.end())
            r.action = la->second;
        auto vio = m_violations.find(id);
        if(vio != m_violations.end())
            r.violations = vio->second;
        r.leaderId = (leaderIdx >= 0) ? static_cast<long>(vehs[leaderIdx].id) : -1;
        r.segmentId = vehs[si].segmentId;
        r.laneIndex = vehs[si].laneIndex;
        r.speed = glm::length(vehs[si].vel);
        r.found = true;
        return r;
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
                // Seed world pose so a runtime-added vehicle never renders at the
                // origin before the first Phase B (init does this for loaded vehicles).
                if(Vehicle* stored = m_world.find(v.id))
                {
                    stored->worldPos = segWorldPos(*segment, stored->position, stored->laneIndex);
                    const glm::vec2 t =
                        segment->geometry().tangentAt(stored->position * segment->length);
                    stored->heading = std::atan2(t.y, t.x);
                }
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
        m_signCleared.erase(id);
        m_laneChangeCooldown.erase(id);
        m_violations.erase(id);
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