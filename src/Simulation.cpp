#include "Simulation.hpp"

#include <algorithm>

namespace tfv
{

    void Simulation::step(double dt)
    {
        std::scoped_lock lock(m_mtx);
        for(auto& [id, v] : m_vehicles)
        {
            v.position += v.speed * static_cast<float>(dt);
            if(v.position > 500.f) // loop demo road
                v.position -= 500.f;
        }
    }

    VehicleMap Simulation::snapshot() const
    {
        std::scoped_lock lock(m_mtx);
        return m_vehicles; // copy
    }

    void Simulation::addVehicle(const Vehicle& v)
    {
        std::scoped_lock lock(m_mtx);
        m_vehicles[v.id] = v;
    }

    void Simulation::removeVehicle(uint64_t id)
    {
        std::scoped_lock lock(m_mtx);
        m_vehicles.erase(id);
    }

} // namespace tfv