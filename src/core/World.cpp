#include "core/World.hpp"

#include <algorithm>

namespace tfv
{
    void World::clear()
    {
        m_vehicles.clear();
        m_index.clear();
    }

    void World::load(std::vector<Vehicle> vehicles)
    {
        m_vehicles = std::move(vehicles);
        std::sort(m_vehicles.begin(), m_vehicles.end(),
                  [](const Vehicle& a, const Vehicle& b) { return a.id < b.id; });

        // Collapse duplicate ids, keeping the LAST occurrence of each (matches the
        // prior map-based `m_vehicles[id] = v` last-wins semantics) so the
        // id -> index invariant holds and every stored vehicle is addressable.
        if(!m_vehicles.empty())
        {
            std::size_t w = 0;
            for(std::size_t r = 0; r < m_vehicles.size(); ++r)
            {
                // Skip all but the last element of a run of equal ids.
                if(r + 1 < m_vehicles.size() && m_vehicles[r + 1].id == m_vehicles[r].id)
                    continue;
                if(w != r)
                    m_vehicles[w] = m_vehicles[r];
                ++w;
            }
            m_vehicles.resize(w);
        }

        rebuildIndex();
    }

    void World::addVehicle(const Vehicle& v)
    {
        auto existing = m_index.find(v.id);
        if(existing != m_index.end())
        {
            m_vehicles[existing->second] = v; // replace in place; id (sort key) unchanged
            return;
        }

        // Insert keeping ascending-id order so iteration stays deterministic.
        auto pos = std::lower_bound(m_vehicles.begin(), m_vehicles.end(), v.id,
                                    [](const Vehicle& a, uint64_t id) { return a.id < id; });
        m_vehicles.insert(pos, v);
        rebuildIndex();
    }

    void World::removeVehicle(uint64_t id)
    {
        auto it = m_index.find(id);
        if(it == m_index.end())
            return;
        m_vehicles.erase(m_vehicles.begin() + static_cast<std::ptrdiff_t>(it->second));
        rebuildIndex();
    }

    Vehicle* World::find(uint64_t id)
    {
        auto it = m_index.find(id);
        return (it != m_index.end()) ? &m_vehicles[it->second] : nullptr;
    }

    const Vehicle* World::find(uint64_t id) const
    {
        auto it = m_index.find(id);
        return (it != m_index.end()) ? &m_vehicles[it->second] : nullptr;
    }

    VehicleMap World::toMap() const
    {
        VehicleMap map;
        map.reserve(m_vehicles.size());
        for(const auto& v : m_vehicles)
            map.emplace(v.id, v);
        return map;
    }

    void World::rebuildIndex()
    {
        m_index.clear();
        m_index.reserve(m_vehicles.size());
        for(std::size_t i = 0; i < m_vehicles.size(); ++i)
            m_index[m_vehicles[i].id] = i; // overwrite: last slot wins if a dup ever remains
    }

} // namespace tfv
