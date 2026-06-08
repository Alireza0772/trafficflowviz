#include "rendering/SceneRenderer.hpp"

#include "agents/Action.hpp"
#include <cmath>
#include <glm/glm.hpp>
#include <unordered_map>

namespace tfv
{
    void SceneRenderer::draw(const VehicleMap& vehicles)
    {
        if(m_graphView)
        {
            drawGraph(vehicles);
            m_lastSnapshot = vehicles;
            return;
        }
        RoadRenderer roadR(m_r, m_panX, m_panY, m_scale, m_antiAliasing, themePalette(m_theme));
        roadR.draw(m_net);
        drawJunctions(); // 3-way / 4-way pads + roundabout islands, on top of the road ribbons
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

    void SceneRenderer::drawGraph(const VehicleMap& vehicles)
    {
        if(!m_net)
            return;
        auto toScreen = [&](float wx, float wy) {
            return std::pair<int, int>{static_cast<int>(wx * m_scale) + m_panX,
                                       static_cast<int>(wy * m_scale) + m_panY};
        };
        auto classColor = [](RoadClass rc) -> RGB8 {
            switch(rc)
            {
            case RoadClass::HIGHWAY:   return {240, 150, 60};
            case RoadClass::ARTERIAL:  return {235, 205, 90};
            case RoadClass::COLLECTOR: return {120, 200, 230};
            default:                   return {150, 160, 175}; // LOCAL / NONE
            }
        };
        // Directed edges: one line per segment, colored by class, thickness by lane count, with an
        // arrowhead near the destination; two-way pairs are nudged apart so both directions show.
        for(const auto& s : m_net->segments())
        {
            const float dx = static_cast<float>(s.x2 - s.x1), dy = static_cast<float>(s.y2 - s.y1);
            const float len = std::sqrt(dx * dx + dy * dy);
            if(len < 1e-3f)
                continue;
            const float nx = -dy / len, ny = dx / len;
            const float off = ((s.pairId != 0) || (s.medianOffset != 0.0f)) ? 3.5f : 0.0f;
            const RoadSegment* seg = m_net->getSegment(s.id);
            const int lanes = seg ? std::max(1, seg->lanes) : 1;
            const auto p1 = toScreen(s.x1 + nx * off, s.y1 + ny * off);
            const auto p2 = toScreen(s.x2 + nx * off, s.y2 + ny * off);
            const RGB8 c = classColor(s.roadClass);
            m_r->setColor(c.r, c.g, c.b, 235);
            m_r->drawLine(p1.first, p1.second, p2.first, p2.second, std::max(1, lanes));
            float ux = static_cast<float>(p2.first - p1.first), uy = static_cast<float>(p2.second - p1.second);
            const float ul = std::sqrt(ux * ux + uy * uy);
            if(ul > 6.0f)
            {
                ux /= ul;
                uy /= ul;
                const float ah = std::max(5.0f, m_scale * 3.0f);
                const float bx = p2.first - ux * ah, by = p2.second - uy * ah;
                const float lx = -uy, ly = ux; // perpendicular
                m_r->drawLine(p2.first, p2.second, static_cast<int>(bx + lx * ah * 0.5f),
                              static_cast<int>(by + ly * ah * 0.5f), 1);
                m_r->drawLine(p2.first, p2.second, static_cast<int>(bx - lx * ah * 0.5f),
                              static_cast<int>(by - ly * ah * 0.5f), 1);
            }
        }
        // Nodes: square marker sized by degree; roundabouts drawn green to stand out.
        for(uint32_t id : m_net->getNodeIds())
        {
            const Node* n = m_net->getNode(id);
            if(!n)
                continue;
            const int deg = static_cast<int>(std::max(n->incoming.size(), n->outgoing.size()));
            if(deg == 0)
                continue;
            const auto p = toScreen(n->pos.x, n->pos.y);
            const float sz = static_cast<float>(std::max(4, std::min(16, 3 + deg * 2)));
            if(n->junction == JunctionStyle::ROUNDABOUT)
                m_r->fillCircle(static_cast<float>(p.first), static_cast<float>(p.second),
                                sz * 0.5f, 90, 200, 120, 255, 16);
            else
                m_r->fillCircle(static_cast<float>(p.first), static_cast<float>(p.second),
                                sz * 0.5f, 235, 235, 245, 255, 14);
        }
        // Vehicles as small dots, colored by the active encoding.
        const float v0 = 13.9f;
        for(const auto& [vid, v] : vehicles)
        {
            (void)vid;
            const auto p = toScreen(v.worldPos.x, v.worldPos.y);
            const RGB8 c = encodeVehicleColor(v, m_encoding, v0);
            const float d = std::max(2.0f, m_scale * 1.2f);
            m_r->fillCircle(static_cast<float>(p.first), static_cast<float>(p.second), d * 0.5f,
                            c.r, c.g, c.b, 255, 10);
        }
    }

    void SceneRenderer::drawJunctions()
    {
        if(!m_net)
            return;
        const ThemePalette pal = themePalette(m_theme);
        const float laneWpx = 3.5f;
        // ONLY roundabouts get a drawn shape (the round island, via true vector circles). 3-/4-way
        // junctions are just the road crossing itself, marked by their signal head.
        for(uint32_t id : m_net->getNodeIds())
        {
            const Node* n = m_net->getNode(id);
            if(!n || n->junction != JunctionStyle::ROUNDABOUT)
                continue;
            const float cx = n->pos.x * m_scale + static_cast<float>(m_panX);
            const float cy = n->pos.y * m_scale + static_cast<float>(m_panY);
            if(n->roundaboutR > 0.0f)
            {
                // Topological roundabout: the ring ROADS already draw the circle, so just fill
                // the centre island inside them.
                const float islandR = std::max(3.0f, (n->roundaboutR - laneWpx * 1.2f)) * m_scale;
                m_r->fillCircle(cx, cy, islandR, 58, 102, 66, 255);
            }
            else
            {
                // Visual-only roundabout (too few arms to ring): paved disc + lane ring + island.
                const int deg = static_cast<int>(std::max(n->incoming.size(), n->outgoing.size()));
                const float rad = std::max(7.0f, std::min(30.0f, deg * 3.2f)) * m_scale;
                m_r->fillCircle(cx, cy, rad, pal.asphalt.r, pal.asphalt.g, pal.asphalt.b, 255);
                m_r->strokeCircle(cx, cy, rad, std::max(1.0f, m_scale), pal.center.r, pal.center.g,
                                  pal.center.b, 255);
                m_r->fillCircle(cx, cy, rad * 0.45f, 58, 102, 66, 255);
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
        const float laneWpx = 3.5f; // per-lane width (world == px), matches makeTwoWay's median math
        const auto& segs = net->segments();

        auto shade = [](RGB8 base, float f) -> RGB8 {
            auto cl = [](float v) {
                return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, v)));
            };
            return RGB8{cl(base.r * f), cl(base.g * f), cl(base.b * f)};
        };
        // Offset a world polyline sideways by `off` along its per-point normals (averaged
        // tangents -> a smooth offset that follows curves).
        auto offsetPoly = [](const std::vector<glm::vec2>& c, float off) {
            const std::size_t n = c.size();
            std::vector<glm::vec2> o(n);
            for(std::size_t i = 0; i < n; ++i)
            {
                glm::vec2 t(0.0f, 0.0f);
                if(i + 1 < n) t += c[i + 1] - c[i];
                if(i > 0) t += c[i] - c[i - 1];
                const float L = glm::length(t);
                o[i] = (L > 1e-5f) ? c[i] + glm::vec2(-t.y / L, t.x / L) * off : c[i];
            }
            return o;
        };
        auto toScreen = [&](const std::vector<glm::vec2>& w) {
            std::vector<glm::vec2> s(w.size());
            for(std::size_t i = 0; i < w.size(); ++i)
                s[i] = glm::vec2(w[i].x * m_scale + static_cast<float>(m_panX),
                                 w[i].y * m_scale + static_cast<float>(m_panY));
            return s;
        };

        for(const auto& s : segs)
        {
            const RoadSegment* seg = net->getSegment(s.id);
            const int lanes = seg ? std::max(1, seg->lanes) : 1;
            const float half = lanes * laneWpx * 0.5f;
            const bool twoway = (s.pairId != 0) || (s.medianOffset != 0.0f);
            const bool oneWay =
                seg && seg->oneway && seg->pairId == 0 && seg->roadClass != RoadClass::NONE;
            RGB8 asphalt = m_pal.asphalt;
            switch(s.roadClass)
            {
            case RoadClass::HIGHWAY:   asphalt = shade(m_pal.asphalt, 1.55f); break;
            case RoadClass::ARTERIAL:  asphalt = shade(m_pal.asphalt, 1.30f); break;
            case RoadClass::COLLECTOR: asphalt = shade(m_pal.asphalt, 1.10f); break;
            case RoadClass::LOCAL:     asphalt = shade(m_pal.asphalt, 0.90f); break;
            default:                   break; // NONE (CSV/hand-authored): base asphalt
            }

            // The road's true centerline: the curve points, or the straight endpoints.
            std::vector<glm::vec2> center;
            if(seg && seg->centerline.size() >= 2)
                center = seg->centerline;
            else
                center = {glm::vec2(static_cast<float>(s.x1), static_cast<float>(s.y1)),
                          glm::vec2(static_cast<float>(s.x2), static_cast<float>(s.y2))};
            if(center.size() < 2)
                continue;

            // This carriageway's centerline (shifted to its side of the median).
            const std::vector<glm::vec2> carriage = offsetPoly(center, s.medianOffset);
            const std::vector<glm::vec2> cScr = toScreen(carriage);
            const float wpx = std::max(1.0f, lanes * laneWpx * m_scale);

            // Asphalt ribbon: one smooth stroke with round joins (no gaps at curve bends).
            m_r->strokePolyline(cScr.data(), static_cast<int>(cScr.size()), wpx, asphalt.r,
                                asphalt.g, asphalt.b, 255, true);

            // Edge lines: white outer shoulder; the median-facing edge is a thicker centre-colour
            // divider on a two-way road, else another white shoulder.
            const auto outer = toScreen(offsetPoly(carriage, half));
            const auto inner = toScreen(offsetPoly(carriage, -half));
            const float ew = std::max(1.0f, m_scale * 0.5f);
            m_r->strokePolyline(outer.data(), static_cast<int>(outer.size()), ew, m_pal.edge.r,
                                m_pal.edge.g, m_pal.edge.b, 255, false);
            if(twoway)
                m_r->strokePolyline(inner.data(), static_cast<int>(inner.size()),
                                    std::max(1.0f, m_scale * 0.8f), m_pal.center.r, m_pal.center.g,
                                    m_pal.center.b, 255, false);
            else
                m_r->strokePolyline(inner.data(), static_cast<int>(inner.size()), ew, m_pal.edge.r,
                                    m_pal.edge.g, m_pal.edge.b, 255, false);

            // Dashed lane dividers between lanes (none on a single-lane carriageway).
            if(lanes > 1)
            {
                m_r->setColor(m_pal.center.r, m_pal.center.g, m_pal.center.b, 180);
                for(int k = 1; k < lanes; ++k)
                {
                    const auto div = toScreen(offsetPoly(carriage, half - k * laneWpx));
                    for(std::size_t i = 0; i + 1 < div.size(); ++i)
                        drawDashedLine(static_cast<int>(div[i].x), static_cast<int>(div[i].y),
                                       static_cast<int>(div[i + 1].x), static_cast<int>(div[i + 1].y));
                }
            }

            // One-way: amber chevrons along the carriageway pointing in travel direction.
            if(oneWay)
            {
                m_r->setColor(250, 225, 110, 255);
                for(std::size_t i = 0; i + 1 < cScr.size(); ++i)
                {
                    glm::vec2 d = cScr[i + 1] - cScr[i];
                    const float L = glm::length(d);
                    if(L < 9.0f)
                        continue;
                    d /= L;
                    const glm::vec2 nrm(-d.y, d.x);
                    const glm::vec2 mid = (cScr[i] + cScr[i + 1]) * 0.5f;
                    const float cz = std::max(3.0f, laneWpx * m_scale * 0.9f);
                    const glm::vec2 tip = mid + d * cz;
                    const glm::vec2 lp = mid - d * (cz * 0.4f) + nrm * (cz * 0.7f);
                    const glm::vec2 rp = mid - d * (cz * 0.4f) - nrm * (cz * 0.7f);
                    m_r->drawLine((int)lp.x, (int)lp.y, (int)tip.x, (int)tip.y, 1);
                    m_r->drawLine((int)rp.x, (int)rp.y, (int)tip.x, (int)tip.y, 1);
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