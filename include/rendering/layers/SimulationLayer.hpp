#ifndef TFV_SIMULATION_LAYER_HPP
#define TFV_SIMULATION_LAYER_HPP

#include "core/Layer.hpp"
#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "rendering/Renderer.hpp"
#include "rendering/SceneRenderer.hpp"
#include <cstdint>
#include <glm/glm.hpp>
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
        virtual bool onEvent(Event& event) override;
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

        // Map a logical-point cursor position to world coordinates (inverse of the
        // vehicle render transform). For inspector vehicle picking + the debug overlay.
        glm::vec2 screenToWorld(float logicalX, float logicalY) const;

        // The vehicle the user clicked (0 = none); used by the inspector / debug overlay.
        uint64_t selectedVehicleId() const { return m_selectedVehicleId; }

      private:
        // drawable-pixels / window-points (1.0 except on hi-DPI displays)
        float framebufferScale() const;

        // Center + scale the camera so the whole road network fits the viewport.
        void fitToView();

        Renderer* m_renderer;
        Simulation* m_simulation;

        // Scene renderer for visualization
        std::unique_ptr<SceneRenderer> m_sceneRenderer;
        
        // Mouse interaction state
        bool m_isDragging = false;
        float m_lastMouseX = 0.0f;
        float m_lastMouseY = 0.0f;
        uint64_t m_selectedVehicleId = 0; // clicked vehicle for the inspector (0 = none)
    };

} // namespace tfv

#endif // TFV_SIMULATION_LAYER_HPP