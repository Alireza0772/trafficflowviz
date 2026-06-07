#ifndef TFV_DEBUG_PERCEPTION_LAYER_HPP
#define TFV_DEBUG_PERCEPTION_LAYER_HPP

#include "core/Layer.hpp"

namespace tfv
{
    class Renderer;
    class Simulation;
    class SimulationLayer;

    /**
     * Spatial perception overlay (Phase 7): for the selected vehicle, draws the FOV
     * sector boundaries and a colored line to each locked F/R/L/R neighbour (what the
     * agent senses). Reads Simulation::inspect() — purely render/read-only. Off by
     * default; onRender() early-returns when disabled or nothing is selected, so it
     * costs nothing then.
     */
    class DebugPerceptionLayer : public Layer
    {
      public:
        DebugPerceptionLayer(Renderer* renderer, Simulation* simulation,
                             SimulationLayer* simulationLayer);

        void onRender() override;

      private:
        Renderer* m_renderer;
        Simulation* m_simulation;
        SimulationLayer* m_simulationLayer;
    };

} // namespace tfv

#endif // TFV_DEBUG_PERCEPTION_LAYER_HPP
