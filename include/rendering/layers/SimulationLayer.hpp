#ifndef TFV_SIMULATION_LAYER_HPP
#define TFV_SIMULATION_LAYER_HPP

#include "core/Layer.hpp"
#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "rendering/Renderer.hpp"
#include "rendering/SceneRenderer.hpp"
#include <memory>
#include <string>

namespace tfv
{

    /**
     * Layer for rendering the traffic simulation
     * This is the base rendering layer (layer 0)
     */
    class SimulationLayer : public Layer
    {
      public:
        SimulationLayer(Renderer* renderer, Simulation* simulation);
        virtual ~SimulationLayer();

        // Layer interface implementation
        virtual void onAttach() override;
        virtual void onDetach() override;
        virtual bool onEvent(void* event) override;
        virtual void onUpdate(double dt) override;
        virtual void onRender() override;
        virtual void onImGuiRender() override;

        // Scene control
        void setPan(float x, float y);
        void setZoom(float zoom);
        float getPanX() const;
        float getPanY() const;
        float getZoom() const;

        // Get road network for use by other layers
        const RoadNetwork* getRoadNetwork() const { return m_simulation->getRoadNetwork(); }

      private:
        Renderer* m_renderer;
        Simulation* m_simulation;

        // Scene renderer for visualization
        std::unique_ptr<SceneRenderer> m_sceneRenderer;
    };

} // namespace tfv

#endif // TFV_SIMULATION_LAYER_HPP