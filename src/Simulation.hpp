#ifndef TFV_SIMULATION_HPP
#define TFV_SIMULATION_HPP

#include <mutex>

#include "TrafficEntity.hpp"

namespace tfv
{

    class Simulation
    {
      public:
        /** Advance physics by `dt` seconds. */
        void step(double dt);

        /** Thread‑safe copy for rendering. */
        VehicleMap snapshot() const;

        // — Mutation API —
        void addVehicle(const Vehicle& v);
        void removeVehicle(uint64_t id);

      private:
        VehicleMap m_vehicles;
        mutable std::mutex m_mtx;
    };

} // namespace tfv
#endif