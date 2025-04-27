#ifndef TFV_IMGUI_LAYER_HPP
#define TFV_IMGUI_LAYER_HPP

#include "alerts/AlertManager.hpp"
#include "core/Layer.hpp"
#include "core/Simulation.hpp"
#include "recording/RecordingManager.hpp"
#include "rendering/layers/SimulationLayer.hpp"
#include <SDL2/SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tfv
{

    /**
     * Layer for Dear ImGui UI rendering
     * Sits at the top of the layer stack (highest z-index)
     */
    class ImGuiLayer : public Layer
    {
      public:
        ImGuiLayer(SDL_Window* window, SDL_Renderer* renderer, Simulation* simulation);
        virtual ~ImGuiLayer();

        // Layer interface implementation
        virtual void onAttach() override;
        virtual void onDetach() override;
        virtual bool onEvent(void* event) override;
        virtual void onUpdate(double dt) override;
        virtual void onRender() override;
        virtual void onImGuiRender() override;

        // Set references to other system components
        void setSimulationLayer(SimulationLayer* layer) { m_simulationLayer = layer; }
        void setAlertManager(AlertManager* manager) { m_alertManager = manager; }
        void setRecordingManager(RecordingManager* manager) { m_recordingManager = manager; }

        // Feature toggles for UI elements
        void showKeybindingsWindow(bool show) { m_showKeybindings = show; }
        bool isKeybindingsWindowVisible() const { return m_showKeybindings; }

        // FPS setting
        void setFPS(int fps) { m_fps = fps; }
        int getFPS() const { return m_fps; }

        // Alert callback
        using AlertUICallback = std::function<void(const std::string& message, uint32_t segmentId)>;
        void setAlertCallback(AlertUICallback callback) { m_alertUICallback = callback; }

      private:
        SDL_Window* m_window;
        SDL_Renderer* m_renderer;
        Simulation* m_simulation;
        SimulationLayer* m_simulationLayer{nullptr};
        AlertManager* m_alertManager{nullptr};
        RecordingManager* m_recordingManager{nullptr};

        bool m_initialized{false};
        bool m_showKeybindings{false};

        // FPS tracking
        int m_fps{0};

        // Alert callback
        AlertUICallback m_alertUICallback;

        // UI helper methods
        void renderMainMenuBar();
        void renderStatusBar();
        void renderKeybindingsWindow();
        void renderDockspace();

        static void HelpMarker(const char* desc);
    };

} // namespace tfv

#endif // TFV_IMGUI_LAYER_HPP