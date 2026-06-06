#ifndef TFV_WORLD_HPP
#define TFV_WORLD_HPP

#include "core/TrafficEntity.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace tfv
{
    /**
     * Data-oriented container for agent (vehicle) state.
     *
     * Phase 1 of the agent-based re-architecture (docs/agent-simulation-design.md):
     * replaces the simulation's std::unordered_map<uint64_t, Vehicle> with a
     * contiguous, deterministically-ordered store. Vehicles are kept sorted by
     * their stable id so iteration order is reproducible across runs and platforms
     * (unordered_map iteration order is not). Lookups by id stay O(1) via an index.
     *
     * This is the substrate that sits behind the Simulation facade. Full
     * structure-of-arrays field splitting and a generational EntityId are deferred
     * to later phases, where perception and brains require them.
     */
    class World
    {
      public:
        void clear();

        /** Bulk load (initialization path); sorts by id once. */
        void load(std::vector<Vehicle> vehicles);

        /** Insert a new vehicle (or replace one with the same id), keeping order. */
        void addVehicle(const Vehicle& v);

        /** Remove a vehicle by id (no-op if absent). */
        void removeVehicle(uint64_t id);

        /** O(1) lookup; nullptr if absent. */
        Vehicle* find(uint64_t id);
        const Vehicle* find(uint64_t id) const;

        std::size_t size() const { return m_vehicles.size(); }
        bool empty() const { return m_vehicles.empty(); }

        /**
         * Contiguous, ascending-id-ordered storage for deterministic iteration.
         * Callers may mutate per-vehicle state (segmentId/position/vel/acc) in place,
         * but MUST NOT change a vehicle's id through this reference — that desyncs the
         * id->index map and breaks the sort invariant. Use removeVehicle/addVehicle
         * to re-key.
         */
        std::vector<Vehicle>& vehicles() { return m_vehicles; }
        const std::vector<Vehicle>& vehicles() const { return m_vehicles; }

        /** Build a VehicleMap view (used by Simulation::snapshot for the renderer). */
        VehicleMap toMap() const;

      private:
        void rebuildIndex();

        std::vector<Vehicle> m_vehicles;                   // kept sorted by id
        std::unordered_map<uint64_t, std::size_t> m_index; // id -> index in m_vehicles
    };

} // namespace tfv

#endif // TFV_WORLD_HPP
