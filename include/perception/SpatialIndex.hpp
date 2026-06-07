#ifndef TFV_SPATIAL_INDEX_HPP
#define TFV_SPATIAL_INDEX_HPP

#include "core/TrafficEntity.hpp"

#include <cmath>
#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

namespace tfv
{
    /**
     * Uniform grid over the network world AABB for O(local) neighbor queries
     * (Phase 5). Holds VEHICLE INDICES into the frozen frame-N vehicle vector.
     * Rebuilt once per tick from that vector (which is ascending-id ordered), so
     * each cell bucket is ascending-id by construction -> deterministic iteration.
     */
    class UniformGrid
    {
      public:
        void setBounds(glm::vec2 mn, glm::vec2 mx, float cellSize);
        void rebuild(const std::vector<Vehicle>& vehs);

        // Visit candidate vehicle indices in the 3x3 cell block around p (cellSize is
        // sized >= the max query radius, so a 3x3 block covers it). fn(std::size_t idx).
        template <class F> void forEachNear(glm::vec2 p, F&& fn) const
        {
            if(m_cols <= 0 || m_rows <= 0)
                return;
            const int cx = static_cast<int>(std::floor((p.x - m_minX) / m_cell));
            const int cy = static_cast<int>(std::floor((p.y - m_minY) / m_cell));
            for(int dy = -1; dy <= 1; ++dy)
                for(int dx = -1; dx <= 1; ++dx)
                {
                    const int nx = cx + dx, ny = cy + dy;
                    if(nx < 0 || ny < 0 || nx >= m_cols || ny >= m_rows)
                        continue;
                    for(std::size_t idx : m_cells[static_cast<std::size_t>(ny) * m_cols + nx])
                        fn(idx);
                }
        }

      private:
        int m_cols{0}, m_rows{0};
        float m_minX{0.0f}, m_minY{0.0f}, m_cell{1.0f};
        std::vector<std::vector<std::size_t>> m_cells;
    };

} // namespace tfv

#endif // TFV_SPATIAL_INDEX_HPP
