#include "rendering/layers/SimulationLayer.hpp"
#include <SDL2/SDL.h>

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
    }

    void SimulationLayer::onDetach()
    {
        m_sceneRenderer.reset();
    }

    bool SimulationLayer::onEvent(void* event)
    {
        if(!event)
            return false;

        SDL_Event* sdlEvent = static_cast<SDL_Event*>(event);

        // Handle pan and zoom events
        if(sdlEvent->type == SDL_KEYDOWN)
        {
            switch(sdlEvent->key.keysym.sym)
            {
            // Pan with arrow keys
            case SDLK_LEFT:
                setPan(getPanX() + 20, getPanY());
                return true;
            case SDLK_RIGHT:
                setPan(getPanX() - 20, getPanY());
                return true;
            case SDLK_UP:
                setPan(getPanX(), getPanY() + 20);
                return true;
            case SDLK_DOWN:
                setPan(getPanX(), getPanY() - 20);
                return true;

            // Zoom controls
            case SDLK_EQUALS: // '+' key (zoom in)
            case SDLK_KP_PLUS:
                setZoom(getZoom() * 1.1f);
                return true;
            case SDLK_MINUS: // '-' key (zoom out)
            case SDLK_KP_MINUS:
                setZoom(getZoom() / 1.1f);
                return true;
            }
        }

        // Zoom with mouse wheel
        if(sdlEvent->type == SDL_MOUSEWHEEL)
        {
            if(sdlEvent->wheel.y > 0)
                setZoom(getZoom() * 1.1f);
            else if(sdlEvent->wheel.y < 0)
                setZoom(getZoom() / 1.1f);
            return true;
        }

        return false;
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

} // namespace tfv