#include "rendering/SceneRenderer.hpp"

#include "agents/Action.hpp"
#include <cmath>
#include <glm/glm.hpp>
#include <unordered_map>

namespace tfv
{
    void SceneRenderer::draw(const VehicleMap& vehicles)
    {
        RoadRenderer roadR(m_r, m_panX, m_panY, m_scale, m_antiAliasing, themePalette(m_theme));
        roadR.draw(m_net);
        VehicleRenderer vehR(m_r, m_panX, m_panY, m_scale, m_antiAliasing, m_encoding);
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

                // Three-lamp signal head: red / amber / green stacked; the active lamp
                // is bright, the others dimmed, inside a dark housing.
                const LightColor cur = m_net->approachColor(segId);
                const int lamp = std::max(3, static_cast<int>(2.5f * m_scale));
                const int gap = std::max(1, lamp / 3);
                const int totalH = lamp * 3 + gap * 2;
                m_r->setColor(18, 18, 22, 255);
                m_r->fillRect(sx - lamp / 2 - 1, sy - totalH / 2 - 1, lamp + 2, totalH + 2);
                auto lampAt = [&](int row, uint8_t lr, uint8_t lg, uint8_t lb, bool on) {
                    const int ly = sy - totalH / 2 + row * (lamp + gap);
                    if(on)
                        m_r->setColor(lr, lg, lb, 255);
                    else
                        m_r->setColor(lr / 5, lg / 5, lb / 5, 255);
                    m_r->fillRect(sx - lamp / 2, ly, lamp, lamp);
                };
                lampAt(0, 235, 45, 45, cur == LightColor::Red);
                lampAt(1, 240, 200, 45, cur == LightColor::Amber);
                lampAt(2, 45, 220, 45, cur == LightColor::Green);
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
        // Per-lane width in world units (== sim.lane_width_m default, which is also what
        // makeTwoWay uses for the median). Keeping the renderer and the median math on the
        // SAME lane width is what makes the two carriageways leave a clean median gap.
        const float laneWpx = 3.5f;
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
            // Ribbon width now tracks the lane count instead of a fixed 10 px. The old fixed
            // width made every road equally wide AND, on a two-way road (median offset 3.5),
            // overlapped the two 10-px carriageways straight through the median — which is why
            // the divider looked wrong. lanes*laneWpx matches the makeTwoWay offset, so opposing
            // ribbons now leave a median gap exactly medianWidth wide.
            const RoadSegment* seg = net->getSegment(s.id);
            const int lanes = seg ? std::max(1, seg->lanes) : 1;
            const float half = lanes * laneWpx * 0.5f;
            const bool twoway = (s.pairId != 0) || (s.medianOffset != 0.0f);
            auto toScreen = [&](float wx, float wy)
            {
                return std::pair<int, int>{static_cast<int>(wx * m_scale) + m_panX,
                                           static_cast<int>(wy * m_scale) + m_panY};
            };
            // Shift the ribbon to this direction's side of the median (0 = one-way -> unchanged).
            const float cx1 = x1 + nx * s.medianOffset, cy1 = y1 + ny * s.medianOffset;
            const float cx2 = x2 + nx * s.medianOffset, cy2 = y2 + ny * s.medianOffset;
            auto a1 = toScreen(cx1 + nx * half, cy1 + ny * half); // outer edge (+normal)
            auto a2 = toScreen(cx2 + nx * half, cy2 + ny * half);
            auto b1 = toScreen(cx1 - nx * half, cy1 - ny * half); // median-facing edge (-normal)
            auto b2 = toScreen(cx2 - nx * half, cy2 - ny * half);

            // Asphalt ribbon (filled quad a1 -> a2 -> b2 -> b1), tinted by road class so the
            // generated hierarchy reads at a glance (brighter asphalt = higher class).
            auto shade = [](RGB8 base, float f) -> RGB8 {
                auto cl = [](float v) {
                    return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, v)));
                };
                return RGB8{cl(base.r * f), cl(base.g * f), cl(base.b * f)};
            };
            RGB8 asphalt = m_pal.asphalt;
            switch(s.roadClass)
            {
            case RoadClass::HIGHWAY:   asphalt = shade(m_pal.asphalt, 1.55f); break;
            case RoadClass::ARTERIAL:  asphalt = shade(m_pal.asphalt, 1.30f); break;
            case RoadClass::COLLECTOR: asphalt = shade(m_pal.asphalt, 1.10f); break;
            case RoadClass::LOCAL:     asphalt = shade(m_pal.asphalt, 0.90f); break;
            default:                   break; // NONE (CSV/hand-authored): base asphalt
            }
            auto V = [&](std::pair<int, int> p) {
                return RVertex{static_cast<float>(p.first), static_cast<float>(p.second),
                               asphalt.r, asphalt.g, asphalt.b, 255};
            };
            m_r->fillQuad(V(a1), V(a2), V(b2), V(b1));

            // Outer (shoulder) edge: solid white.
            m_r->setColor(m_pal.edge.r, m_pal.edge.g, m_pal.edge.b, 255);
            m_r->drawLine(a1.first, a1.second, a2.first, a2.second, 1);
            // Median-facing edge: a solid divider (center color) for a two-way road — this is
            // the line against oncoming traffic — otherwise the ordinary white shoulder edge.
            if(twoway)
                m_r->setColor(m_pal.center.r, m_pal.center.g, m_pal.center.b, 255);
            m_r->drawLine(b1.first, b1.second, b2.first, b2.second, twoway ? 2 : 1);

            // Dashed lane dividers BETWEEN lanes (none on a single-lane carriageway). Offsets are
            // measured inward from the outer edge so they sit on the true lane boundaries.
            if(lanes > 1)
            {
                m_r->setColor(m_pal.center.r, m_pal.center.g, m_pal.center.b, 180);
                for(int k = 1; k < lanes; ++k)
                {
                    const float off = half - k * laneWpx;
                    auto d1 = toScreen(cx1 + nx * off, cy1 + ny * off);
                    auto d2 = toScreen(cx2 + nx * off, cy2 + ny * off);
                    drawDashedLine(d1.first, d1.second, d2.first, d2.second);
                }
            }
        }
    }

    void RoadRenderer::drawDashedLine(int x1, int y1, int x2, int y2)
    {
        // Scale the dash pattern with zoom so it reads consistently (and avoids a huge
        // number of sub-quads on long segments at high zoom).
        const int dashLen = std::max(3, static_cast<int>(m_scale * 1.5f));
        const int gapLen = dashLen;
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
                                     bool antiAliasing, ColorEncoding encoding)
        : m_r(renderer), m_panX(panX), m_panY(panY), m_scale(scale), m_antiAliasing(antiAliasing),
          m_encoding(encoding)
    {
        // Set anti-aliasing on the renderer
        m_r->setAntiAliasing(antiAliasing);
    }

    void VehicleRenderer::draw(const VehicleMap& vehicles, const RoadNetwork* const net)
    {
        (void)net; // positioning now comes from Vehicle.worldPos/heading (computed by Simulation)
        const float v0 = 13.9f; // desired-speed cap for the speed gradient
        for(const auto& entry : vehicles)
        {
            const Vehicle& v = entry.second;
            const float ux = std::cos(v.heading), uy = std::sin(v.heading);
            const float nx = -uy, ny = ux; // lateral
            const float sx = v.worldPos.x * m_scale + static_cast<float>(m_panX);
            const float sy = v.worldPos.y * m_scale + static_cast<float>(m_panY);

            const RGB8 c = encodeVehicleColor(v, m_encoding, v0);

            if(m_scale < 1.5f)
            {
                // Too zoomed out for a body: a single colored point.
                m_r->setColor(c.r, c.g, c.b, 255);
                m_r->drawPoint(static_cast<int>(sx), static_cast<int>(sy));
                continue;
            }

            // Oriented body: length x ~2 m width (world meters) -> framebuffer px.
            const float halfL = std::max(2.0f, v.length * 0.5f * m_scale);
            const float halfW = std::max(1.5f, 1.0f * m_scale); // ~2 m wide
            auto P = [&](float fwd, float lat) {
                return RVertex{sx + ux * fwd + nx * lat, sy + uy * fwd + ny * lat,
                               c.r, c.g, c.b, 255};
            };
            m_r->fillQuad(P(halfL, -halfW), P(halfL, halfW), P(-halfL, halfW), P(-halfL, -halfW));

            // Rear brake (red) / turn-signal (amber) light dab over the body.
            const int rx = static_cast<int>(sx - ux * halfL);
            const int ry = static_cast<int>(sy - uy * halfL);
            if(v.lightBits & light::BRAKE)
            {
                const int d = std::max(2, static_cast<int>(m_scale * 0.8f));
                m_r->setColor(250, 60, 45, 255);
                m_r->fillRect(rx - d / 2, ry - d / 2, d, d);
            }
            else if(v.lightBits & (light::LEFT | light::RIGHT))
            {
                const int d = std::max(2, static_cast<int>(m_scale * 0.7f));
                m_r->setColor(248, 200, 40, 255);
                m_r->fillRect(rx - d / 2, ry - d / 2, d, d);
            }
        }
    }
} // namespace tfv