#include "rendering/SceneRenderer.hpp"

#include <cmath>
#include <glm/glm.hpp>

namespace tfv
{
    void SceneRenderer::draw(const VehicleMap& vehicles)
    {
        RoadRenderer roadR(m_r, m_panX, m_panY, m_scale);
        roadR.draw(m_net);
        VehicleRenderer vehR(m_r, m_panX, m_panY, m_scale);
        vehR.draw(vehicles, m_net);
    }

    void RoadRenderer::draw(const RoadNetwork* net)
    {
        if(!net)
            return;
        const auto& segs = net->segments();
        for(const auto& s : segs)
        {
            float x1 = static_cast<float>(s.x1), y1 = static_cast<float>(s.y1);
            float x2 = static_cast<float>(s.x2), y2 = static_cast<float>(s.y2);
            float dx = x2 - x1, dy = y2 - y1;
            float len = std::sqrt(dx * dx + dy * dy);
            if(len == 0)
                continue;
            float nx = -dy / len, ny = dx / len;
            float half = roadWidth * 0.5f;
            auto toScreen = [&](float wx, float wy)
            {
                return std::pair<int, int>{static_cast<int>(wx * m_scale) + m_panX,
                                           static_cast<int>(wy * m_scale) + m_panY};
            };
            auto a1 = toScreen(x1 + nx * half, y1 + ny * half);
            auto a2 = toScreen(x2 + nx * half, y2 + ny * half);
            auto b1 = toScreen(x1 - nx * half, y1 - ny * half);
            auto b2 = toScreen(x2 - nx * half, y2 - ny * half);
            m_r->drawLine(a1.first, a1.second, a2.first, a2.second);
            m_r->drawLine(b1.first, b1.second, b2.first, b2.second);
            m_r->setColor(140, 140, 140, 255);
            drawDashedLine(a1.first, a1.second, b1.first, b1.second);
        }
    }

    void RoadRenderer::drawDashedLine(int x1, int y1, int x2, int y2)
    {
        const int dashLen = 4, gapLen = 4;
        float dx = x2 - x1, dy = y2 - y1;
        float dist = std::sqrt(dx * dx + dy * dy);
        if(dist == 0)
            return;
        float vx = dx / dist, vy = dy / dist;
        float cx = static_cast<float>(x1), cy = static_cast<float>(y1);
        int segments = static_cast<int>(dist / (dashLen + gapLen));
        for(int i = 0; i < segments; ++i)
        {
            m_r->drawLine(static_cast<int>(cx), static_cast<int>(cy),
                          static_cast<int>(cx + vx * dashLen), static_cast<int>(cy + vy * dashLen));
            cx += vx * (dashLen + gapLen);
            cy += vy * (dashLen + gapLen);
        }
    }

    VehicleRenderer::VehicleRenderer(IRenderer* renderer, int panX, int panY, float scale)
        : m_r(renderer), m_panX(panX), m_panY(panY), m_scale(scale)
    {
    }

    void VehicleRenderer::draw(const VehicleMap& vehicles, const RoadNetwork* const net)
    {
        if(!net || net->segments().empty())
            return;
        const auto& segs = net->segments();
        for(const auto& [id, v] : vehicles)
        {
            if(v.segmentId >= segs.size())
                continue;
            const auto& s = segs[v.segmentId];
            float x1 = static_cast<float>(s.x1), y1 = static_cast<float>(s.y1);
            float x2 = static_cast<float>(s.x2), y2 = static_cast<float>(s.y2);
            float dx = x2 - x1, dy = y2 - y1;
            float len = std::sqrt(dx * dx + dy * dy);
            if(len == 0)
                continue;
            float ux = dx / len, uy = dy / len;
            float t = v.position; // position is now 0..1
            float wx = x1 + ux * (len * t);
            float wy = y1 + uy * (len * t);
            int sx = static_cast<int>(wx * m_scale) + m_panX;
            int sy = static_cast<int>(wy * m_scale) + m_panY;
            m_r->setColor(0, 200, 0, 255);
            if(m_scale < 2.0f)
            {
                m_r->drawPoint(sx, sy);
            }
            else
            {
                int arrowLen = static_cast<int>(5 * m_scale);
                int ex = sx + static_cast<int>(ux * arrowLen);
                int ey = sy + static_cast<int>(uy * arrowLen);
                m_r->drawLine(sx, sy, ex, ey);
                // Draw arrowhead
                float nx = -uy, ny = ux; // normal vector
                int ax1 = ex - static_cast<int>((ux * 8 + nx * 4) * m_scale);
                int ay1 = ey - static_cast<int>((uy * 8 + ny * 4) * m_scale);
                int ax2 = ex - static_cast<int>((ux * 8 - nx * 4) * m_scale);
                int ay2 = ey - static_cast<int>((uy * 8 - ny * 4) * m_scale);
                m_r->drawLine(ex, ey, ax1, ay1);
                m_r->drawLine(ex, ey, ax2, ay2);
            }
        }
    }
} // namespace tfv