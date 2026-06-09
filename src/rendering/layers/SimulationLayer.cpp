#include "rendering/layers/SimulationLayer.hpp"
#include "core/Events/KeyEvent.hpp"
#include "core/Events/MouseEvent.hpp"
#include "utils/LoggingManager.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace tfv
{

    SimulationLayer::SimulationLayer(Renderer* renderer, Simulation* simulation)
        : m_renderer(renderer), m_simulation(simulation)
    {
        setName("SimulationLayer");
        setZIndex(0); // Base layer
    }

    SimulationLayer::~SimulationLayer()
    {
        onDetach();
    }

    void SimulationLayer::onAttach()
    {
        m_sceneRenderer = std::make_unique<SceneRenderer>(m_renderer);
        m_sceneRenderer->setNetwork(m_simulation->getRoadNetwork());
        fitToView(); // frame the network so it is not stranded in a corner / off-screen
    }

    void SimulationLayer::fitToView()
    {
        const RoadNetwork* net = m_simulation ? m_simulation->getRoadNetwork() : nullptr;
        if(!net)
            return;
        const auto& segs = net->segments();
        if(segs.empty())
            return;

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = -std::numeric_limits<float>::max();
        float maxY = -std::numeric_limits<float>::max();
        for(const auto& s : segs)
        {
            minX = std::min({minX, float(s.x1), float(s.x2)});
            maxX = std::max({maxX, float(s.x1), float(s.x2)});
            minY = std::min({minY, float(s.y1), float(s.y2)});
            maxY = std::max({maxY, float(s.y1), float(s.y2)});
        }
        const float bw = std::max(1.0f, maxX - minX);
        const float bh = std::max(1.0f, maxY - minY);

        // Viewport in scene (framebuffer pixel) space. Backend-agnostic (SDL or Metal).
        int ow = 1280, oh = 720;
        m_renderer->getDrawableSize(ow, oh);

        const float scale = 0.9f * std::min(float(ow) / bw, float(oh) / bh);
        const float cx = 0.5f * (minX + maxX);
        const float cy = 0.5f * (minY + maxY);
        const int panX = static_cast<int>(0.5f * ow - cx * scale);
        const int panY = static_cast<int>(0.5f * oh - cy * scale);
        m_sceneRenderer->setZoom(scale);
        m_sceneRenderer->setPan(panX, panY);

        LOG_INFO("fitToView: network bbox ({minx},{miny})-({maxx},{maxy}) -> scale {s}",
                 PARAM(minx, minX), PARAM(miny, minY), PARAM(maxx, maxX), PARAM(maxy, maxY),
                 PARAM(s, scale));
    }

    void SimulationLayer::onDetach()
    {
        m_sceneRenderer.reset();
    }

    glm::vec2 SimulationLayer::screenToWorld(float logicalX, float logicalY) const
    {
        // Inverse of the vehicle render transform screen_px = world*zoom + pan, where the
        // cursor arrives in logical points and the scene works in framebuffer pixels.
        const float fb = framebufferScale();
        const float z = std::max(0.01f, getZoom());
        return glm::vec2((logicalX * fb - getPanX()) / z, (logicalY * fb - getPanY()) / z);
    }

    bool SimulationLayer::onEvent(Event& event)
    {
        // Camera controls: left-drag to pan, wheel / +- to zoom, arrows to pan.
        // (Events that ImGui wants are already filtered out at the window level.)
        switch(event.getEventType())
        {
        case EventType::MouseButtonPressed:
        {
            auto& e = static_cast<MouseButtonPressedEvent&>(event);
            if(e.getMouseButton() == MouseButton::Left)
            {
                m_isDragging = true;
                m_lastMouseX = e.getX();
                m_lastMouseY = e.getY();
                // Pick the nearest vehicle to the cursor (within ~14 logical px) for the
                // inspector; clears the selection if the click is on empty road.
                const glm::vec2 w = screenToWorld(e.getX(), e.getY());
                const float z = std::max(0.01f, getZoom());
                const float pr = 14.0f * framebufferScale() / z; // pick radius in world units
                uint64_t best = 0;
                float bestD2 = pr * pr;
                for(const auto& [id, v] : m_simulation->snapshot())
                {
                    const float dx = v.worldPos.x - w.x, dy = v.worldPos.y - w.y;
                    const float d2 = dx * dx + dy * dy;
                    if(d2 <= bestD2)
                    {
                        bestD2 = d2;
                        best = id;
                    }
                }
                m_selectedVehicleId = best;
                return true;
            }
            return false;
        }
        case EventType::MouseButtonReleased:
        {
            auto& e = static_cast<MouseButtonReleasedEvent&>(event);
            if(e.getMouseButton() == MouseButton::Left)
            {
                m_isDragging = false;
                return true;
            }
            return false;
        }
        case EventType::MouseMoved:
        {
            auto& e = static_cast<MouseMovedEvent&>(event);
            if(!m_isDragging)
                return false;
            float dx = e.getX() - m_lastMouseX;
            float dy = e.getY() - m_lastMouseY;
            m_lastMouseX = e.getX();
            m_lastMouseY = e.getY();
            // Mouse deltas are in logical points; the scene pans in framebuffer
            // pixels, so scale by the DPI factor to keep the world under the cursor.
            float s = framebufferScale();
            setPan(getPanX() + dx * s, getPanY() + dy * s);
            return true;
        }
        case EventType::MouseScrolled:
        {
            auto& e = static_cast<MouseScrolledEvent&>(event);
            float dy = e.getYOffset();
            // Deadzone: ignore scroll noise / horizontal-swipe components so the
            // sign cannot jitter. Zoom continuously and proportionally to the
            // delta (monotonic, never "in and out at once").
            if(std::fabs(dy) < 0.01f)
                return true;

            float oldZoom = getZoom();
            float newZoom = std::clamp(oldZoom * std::pow(1.1f, dy), 0.1f, 100.0f);
            if(newZoom == oldZoom)
                return true; // already at a zoom limit

            // Zoom toward the cursor: keep the world point under the mouse fixed.
            // Scene transform is screen_px = world * zoom + pan; the mouse arrives
            // in logical points, so convert to framebuffer pixels first.
            float fb = framebufferScale();
            float mx = e.getX() * fb;
            float my = e.getY() * fb;
            float ratio = newZoom / oldZoom;
            setZoom(newZoom);
            setPan(mx - (mx - getPanX()) * ratio, my - (my - getPanY()) * ratio);
            return true;
        }
        case EventType::KeyPressed:
        {
            auto& e = static_cast<KeyPressedEvent&>(event);
            const float panStep = 25.0f;
            switch(e.getKeyCode())
            {
            case SDLK_LEFT:  setPan(getPanX() + panStep, getPanY()); return true;
            case SDLK_RIGHT: setPan(getPanX() - panStep, getPanY()); return true;
            case SDLK_UP:    setPan(getPanX(), getPanY() + panStep); return true;
            case SDLK_DOWN:  setPan(getPanX(), getPanY() - panStep); return true;
            case SDLK_PLUS:
            case SDLK_EQUALS:
            case SDLK_KP_PLUS:  setZoom(std::clamp(getZoom() * 1.1f, 0.1f, 100.0f)); return true;
            case SDLK_MINUS:
            case SDLK_KP_MINUS: setZoom(std::clamp(getZoom() * 0.9f, 0.1f, 100.0f)); return true;
            default: return false;
            }
        }
        default:
            return false;
        }
    }

    void SimulationLayer::onUpdate(double dt)
    {
        // The simulation update is now handled by the Engine, not by this layer
        // This only updates the scene
        m_sceneRenderer->update(dt);
    }

    void SimulationLayer::onRender()
    {
        // Get current vehicle snapshot from simulation
        VehicleMap vehicles = m_simulation->snapshot();

        // Draw scene contents with the latest vehicle data
        m_sceneRenderer->draw(vehicles);
    }

    void SimulationLayer::onImGuiRender()
    {
        // No ImGui rendering needed for this layer
    }

    // Scene control methods
    void SimulationLayer::setPan(float x, float y)
    {
        m_sceneRenderer->setPan(x, y);
    }

    void SimulationLayer::setZoom(float zoom)
    {
        m_sceneRenderer->setZoom(zoom);
    }

    float SimulationLayer::getPanX() const
    {
        return m_sceneRenderer->getPanX();
    }

    float SimulationLayer::getPanY() const
    {
        return m_sceneRenderer->getPanY();
    }

    float SimulationLayer::getZoom() const
    {
        return m_sceneRenderer->getZoom();
    }

    float SimulationLayer::framebufferScale() const
    {
        if(!m_renderer)
            return 1.0f;
        int ww = 0, wh = 0;
        m_renderer->getWindowSize(ww, wh); // logical points
        if(ww > 0)
        {
            int rw = ww, rh = wh;
            m_renderer->getDrawableSize(rw, rh); // framebuffer pixels (backend-agnostic)
            return static_cast<float>(rw) / static_cast<float>(ww);
        }
        return 1.0f;
    }

} // namespace tfv