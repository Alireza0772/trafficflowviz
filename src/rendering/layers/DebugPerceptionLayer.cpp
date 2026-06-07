#include "rendering/layers/DebugPerceptionLayer.hpp"

#include "core/Simulation.hpp" // pulls perception/PerceptionSystem.hpp (SensedNeighbor/PerceptionParams)
#include "core/TrafficEntity.hpp"
#include "rendering/Renderer.hpp"
#include "rendering/layers/SimulationLayer.hpp"

#include <cmath>

namespace tfv
{
    DebugPerceptionLayer::DebugPerceptionLayer(Renderer* renderer, Simulation* simulation,
                                               SimulationLayer* simulationLayer)
        : m_renderer(renderer), m_simulation(simulation), m_simulationLayer(simulationLayer)
    {
        setName("DebugPerceptionLayer");
        setZIndex(2);       // above SimulationLayer(0)/HeatmapLayer(1), below ImGui
        setEnabled(false);  // opt-in overlay
    }

    void DebugPerceptionLayer::onRender()
    {
        if(!isEnabled() || !m_renderer || !m_simulation || !m_simulationLayer)
            return;
        const uint64_t id = m_simulationLayer->selectedVehicleId();
        if(id == 0)
            return;
        const VehicleInspection ins = m_simulation->inspect(id);
        if(!ins.found)
            return;
        const VehicleMap snap = m_simulation->snapshot();
        auto sit = snap.find(id);
        if(sit == snap.end())
            return;

        const Vehicle& self = sit->second;
        const glm::vec2 sp = m_simulationLayer->worldToScreen(self.worldPos);
        const float zoom = m_simulationLayer->getZoom();
        const PerceptionParams pp = m_simulation->perceptionParams();
        const float h = self.heading;
        const float kDeg = 3.14159265358979f / 180.0f;
        const float kPi = 3.14159265358979f;

        // Highlight the selected vehicle.
        m_renderer->setColor(255, 255, 0, 255);
        m_renderer->drawRect(static_cast<int>(sp.x) - 6, static_cast<int>(sp.y) - 6, 12, 12);

        // FOV sector boundary rays (front/rear half-angles), in screen space.
        auto ray = [&](float ang, float lenWorld) {
            const float len = lenWorld * zoom;
            const int ex = static_cast<int>(sp.x + std::cos(h + ang) * len);
            const int ey = static_cast<int>(sp.y + std::sin(h + ang) * len);
            m_renderer->drawLine(static_cast<int>(sp.x), static_cast<int>(sp.y), ex, ey, 1);
        };
        m_renderer->setColor(120, 120, 160, 200);
        ray(pp.sectorFrontDeg * kDeg, pp.rangeFront);
        ray(-pp.sectorFrontDeg * kDeg, pp.rangeFront);
        ray(kPi - pp.sectorRearDeg * kDeg, pp.rangeRear);
        ray(-(kPi - pp.sectorRearDeg * kDeg), pp.rangeRear);

        // A colored line to each locked neighbour: green=front, blue=rear, yellow=left,
        // magenta=right.
        static const uint8_t col[4][3] = {
            {60, 220, 60}, {80, 140, 255}, {240, 220, 40}, {230, 80, 230}};
        for(int s = 0; s < 4; ++s)
        {
            const SensedNeighbor& n = ins.sectors[s];
            if(!n.valid)
                continue;
            auto nit = snap.find(n.id);
            if(nit == snap.end())
                continue;
            const glm::vec2 np = m_simulationLayer->worldToScreen(nit->second.worldPos);
            m_renderer->setColor(col[s][0], col[s][1], col[s][2], 255);
            m_renderer->drawLine(static_cast<int>(sp.x), static_cast<int>(sp.y),
                                 static_cast<int>(np.x), static_cast<int>(np.y), 2);
            m_renderer->drawRect(static_cast<int>(np.x) - 4, static_cast<int>(np.y) - 4, 8, 8);
        }
    }

} // namespace tfv
