#include "perception/SpatialIndex.hpp"

#include <algorithm>
#include <cmath>

namespace tfv
{
    void UniformGrid::setBounds(glm::vec2 mn, glm::vec2 mx, float cellSize)
    {
        m_minX = mn.x;
        m_minY = mn.y;
        m_cell = (cellSize > 1e-3f) ? cellSize : 1.0f;
        // Compute dims in double and clamp, so a malformed/huge extent can't overflow
        // the int cast (UB) or trigger an enormous allocation.
        constexpr int kMaxDim = 1024;
        const double colsd = std::floor(static_cast<double>(mx.x - mn.x) / m_cell) + 2.0;
        const double rowsd = std::floor(static_cast<double>(mx.y - mn.y) / m_cell) + 2.0;
        m_cols = static_cast<int>(std::clamp(colsd, 1.0, static_cast<double>(kMaxDim)));
        m_rows = static_cast<int>(std::clamp(rowsd, 1.0, static_cast<double>(kMaxDim)));
        m_cells.assign(static_cast<std::size_t>(m_cols) * m_rows, {});
    }

    void UniformGrid::rebuild(const std::vector<Vehicle>& vehs)
    {
        for(auto& c : m_cells)
            c.clear();
        if(m_cols <= 0 || m_rows <= 0)
            return;
        for(std::size_t i = 0; i < vehs.size(); ++i) // vehs is ascending-id -> buckets are too
        {
            int cx = static_cast<int>(std::floor((vehs[i].worldPos.x - m_minX) / m_cell));
            int cy = static_cast<int>(std::floor((vehs[i].worldPos.y - m_minY) / m_cell));
            cx = std::clamp(cx, 0, m_cols - 1); // clamp transient out-of-bounds into edge cells
            cy = std::clamp(cy, 0, m_rows - 1);
            m_cells[static_cast<std::size_t>(cy) * m_cols + cx].push_back(i);
        }
    }

} // namespace tfv
