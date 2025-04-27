#ifndef TFV_HEATMAP_LAYER_HPP
#define TFV_HEATMAP_LAYER_HPP

#include "core/Layer.hpp"
#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "rendering/HeatmapRenderer.hpp"
#include "rendering/Renderer.hpp"
#include "rendering/layers/SimulationLayer.hpp"
#include <memory>

namespace tfv
{

    /**
     * Layer for rendering the traffic heatmap visualization
     */
    class HeatmapLayer : public Layer
    {
      public:
        HeatmapLayer(Renderer* renderer, Simulation* simulation, SimulationLayer* simLayer);
        virtual ~HeatmapLayer();

        // Layer interface implementation
        virtual void onAttach() override;
        virtual void onDetach() override;
        virtual bool onEvent(void* event) override;
        virtual void onUpdate(double dt) override;
        virtual void onRender() override;
        virtual void onImGuiRender() override;

      private:
        Renderer* m_renderer;
        Simulation* m_simulation;
        SimulationLayer* m_simulationLayer;
        std::unique_ptr<HeatmapRenderer> m_heatmapRenderer;
    };

} // namespace tfv

#endif // TFV_HEATMAP_LAYER_HPP