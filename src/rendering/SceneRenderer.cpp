#include "rendering/SceneRenderer.hpp"

#include "agents/Action.hpp"
#include <cmath>
#include <glm/glm.hpp>
#include <unordered_map>

namespace tfv
{
    void SceneRenderer::draw(const VehicleMap& vehicles)
    {
        RoadRenderer roadR(m_r, m_panX, m_panY, m_scale, m_antiAliasing);
        roadR.draw(m_net);
        VehicleRenderer vehR(m_r, m_panX, m_panY, m_scale, m_antiAliasing);
        vehR.draw(vehicles, m_net);

        drawSigns();
        drawLights();

        // Store the snapshot for later renders
        m_lastSnapshot = vehicles;
    }

    void SceneRenderer::drawLights()
    {
        if(!m_net || m_net->intersections().empty())
            return;
        const auto& segs = m_net->segments();
        std::unordered_map<uint32_t, const RoadVisual*> byId;
        byId.reserve(segs.size());
        for(const auto& s : segs)
            byId.emplace(s.id, &s);

        for(const auto& [nodeId, x] : m_net->intersections())
        {
            (void)nodeId;
            for(uint32_t segId : x.approaches)
            {
                auto it = byId.find(segId);
                if(it == byId.end())
                    continue;
                const RoadVisual* rv = it->second;

                // Marker near the stop line (end of the approach segment).
                const float t = 0.92f;
                const float wx = rv->x1 + (rv->x2 - rv->x1) * t;
                const float wy = rv->y1 + (rv->y2 - rv->y1) * t;
                const int sx = static_cast<int>(wx * m_scale) + m_panX;
                const int sy = static_cast<int>(wy * m_scale) + m_panY;

                switch(m_net->approachColor(segId))
                {
                case LightColor::Green: m_r->setColor(40, 220, 40, 255); break;
                case LightColor::Amber: m_r->setColor(240, 200, 40, 255); break;
                case LightColor::Red:   m_r->setColor(230, 40, 40, 255); break;
                }
                const int sz = std::max(6, static_cast<int>(6 * m_scale));
                m_r->fillRect(sx - sz / 2, sy - sz / 2, sz, sz);
            }
        }
    }

    void SceneRenderer::drawSigns()
    {
        if(!m_net)
            return;
        const auto& segs = m_net->segments();
        for(const auto& [sid, sign] : m_net->signs())
        {
            const RoadVisual* rv = nullptr;
            for(const auto& s : segs)
                if(s.id == sign.segmentId)
                {
                    rv = &s;
                    break;
                }
            if(!rv)
                continue;

            const float wx = rv->x1 + (rv->x2 - rv->x1) * sign.pos;
            const float wy = rv->y1 + (rv->y2 - rv->y1) * sign.pos;
            const int sx = static_cast<int>(wx * m_scale) + m_panX;
            const int sy = static_cast<int>(wy * m_scale) + m_panY;

            switch(sign.type)
            {
            case SignType::STOP:        m_r->setColor(220, 40, 40, 255); break;
            case SignType::YIELD:       m_r->setColor(235, 200, 40, 255); break;
            case SignType::SPEED_LIMIT: m_r->setColor(240, 240, 240, 255); break;
            default:                    m_r->setColor(180, 180, 180, 255); break;
            }
            const int sz = std::max(5, static_cast<int>(5 * m_scale));
            m_r->fillRect(sx - sz / 2, sy - sz / 2, sz, sz);
        }
    }

    void SceneRenderer::update(double dt)
    {
        // Animation logic can be added here if needed
    }

    void SceneRenderer::render()
    {
        // m_lastSnapshot might be empty if no vehicles have been stored yet
        // This is just a placeholder implementation - ideally we would get new vehicle data each
        // frame
        if(m_lastSnapshot.empty())
        {
            // Draw empty roads if no vehicle data
            RoadRenderer roadR(m_r, m_panX, m_panY, m_scale, m_antiAliasing);
            roadR.draw(m_net);
        }
        else
        {
            // Use the latest snapshot
            draw(m_lastSnapshot);
        }
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

            // Set color for roads
            m_r->setColor(200, 200, 200, 255);

            // Draw the road edges
            m_r->drawLine(a1.first, a1.second, a2.first, a2.second, 2);
            m_r->drawLine(b1.first, b1.second, b2.first, b2.second, 2);

            // Draw center line
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

        // Make sure the dashed line is visible with proper thickness
        int thickness = std::max(1, static_cast<int>(m_scale / 4.0f));

        for(int i = 0; i < segments; ++i)
        {
            int startX = static_cast<int>(cx);
            int startY = static_cast<int>(cy);
            int endX = static_cast<int>(cx + vx * dashLen);
            int endY = static_cast<int>(cy + vy * dashLen);

            // Draw line with proper thickness
            m_r->drawLine(startX, startY, endX, endY, thickness);

            cx += vx * (dashLen + gapLen);
            cy += vy * (dashLen + gapLen);
        }
    }

    VehicleRenderer::VehicleRenderer(Renderer* renderer, int panX, int panY, float scale,
                                     bool antiAliasing)
        : m_r(renderer), m_panX(panX), m_panY(panY), m_scale(scale), m_antiAliasing(antiAliasing)
    {
        // Set anti-aliasing on the renderer
        m_r->setAntiAliasing(antiAliasing);
    }

    void VehicleRenderer::draw(const VehicleMap& vehicles, const RoadNetwork* const net)
    {
        (void)net; // positioning now comes from Vehicle.worldPos/heading (computed by Simulation)
        for(const auto& entry : vehicles)
        {
            const Vehicle& v = entry.second;
            const float ux = std::cos(v.heading), uy = std::sin(v.heading);
            int sx = static_cast<int>(v.worldPos.x * m_scale) + m_panX;
            int sy = static_cast<int>(v.worldPos.y * m_scale) + m_panY;

            // Brake light: tint red when braking; otherwise the usual green.
            if(v.lightBits & light::BRAKE)
                m_r->setColor(235, 70, 50, 255);
            else
                m_r->setColor(50, 200, 50, 255);

            if(m_scale < 2.0f)
            {
                // At very low zoom levels, just draw a point
                m_r->drawPoint(sx, sy);
            }
            else
            {
                // Calculate appropriate arrow size based on scale
                int arrowLen = std::max(3, static_cast<int>(5 * m_scale));
                int arrowWidth = std::max(1, static_cast<int>(m_scale / 2));

                int ex = sx + static_cast<int>(ux * arrowLen);
                int ey = sy + static_cast<int>(uy * arrowLen);

                // Draw line with proper thickness
                m_r->drawLine(sx, sy, ex, ey, arrowWidth);

                // Draw arrowhead
                float nx = -uy, ny = ux; // normal vector
                int ax1 = ex - static_cast<int>((ux * arrowLen * 0.5f + nx * arrowLen * 0.3f));
                int ay1 = ey - static_cast<int>((uy * arrowLen * 0.5f + ny * arrowLen * 0.3f));
                int ax2 = ex - static_cast<int>((ux * arrowLen * 0.5f - nx * arrowLen * 0.3f));
                int ay2 = ey - static_cast<int>((uy * arrowLen * 0.5f - ny * arrowLen * 0.3f));

                // Draw arrowhead lines
                m_r->drawLine(ex, ey, ax1, ay1, arrowWidth);
                m_r->drawLine(ex, ey, ax2, ay2, arrowWidth);
            }
        }
    }
} // namespace tfv