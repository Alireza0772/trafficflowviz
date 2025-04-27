#include "rendering/layers/HeatmapLayer.hpp"

namespace tfv
{

    HeatmapLayer::HeatmapLayer(Renderer* renderer, Simulation* simulation,
                               SimulationLayer* simLayer)
        : m_renderer(renderer), m_simulation(simulation), m_simulationLayer(simLayer)
    {
        setName("HeatmapLayer");
        setZIndex(1);      // Above simulation layer but below UI
        setEnabled(false); // Disabled by default
    }

    HeatmapLayer::~HeatmapLayer()
    {
        onDetach();
    }

    void HeatmapLayer::onAttach()
    {
        m_heatmapRenderer = std::make_unique<HeatmapRenderer>(m_renderer);
    }

    void HeatmapLayer::onDetach()
    {
        m_heatmapRenderer.reset();
    }

    bool HeatmapLayer::onEvent(void* event)
    {
        // This layer doesn't handle any events directly
        return false;
    }

    void HeatmapLayer::onUpdate(double dt)
    {
        // No specific update logic needed
    }

    void HeatmapLayer::onRender()
    {
        if(!m_simulationLayer || !m_simulation)
            return;

        // Get the road network using the getter method
        const RoadNetwork* network = m_simulationLayer->getRoadNetwork();
        if(!network)
            return;

        // Draw the heatmap based on congestion levels
        m_heatmapRenderer->draw(network, m_simulation->getCongestionLevels(),
                                m_simulationLayer->getPanX(), m_simulationLayer->getPanY(),
                                m_simulationLayer->getZoom());
    }

    void HeatmapLayer::onImGuiRender()
    {
        // No ImGui rendering needed for this layer
    }

} // namespace tfv