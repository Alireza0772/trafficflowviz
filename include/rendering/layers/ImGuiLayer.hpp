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
        virtual bool onEvent(Event& event) override;
        virtual void onUpdate(double dt) override;
        virtual void onRender() override;
        virtual void onImGuiRender() override;

        // Set references to other system components
        void setSimulationLayer(SimulationLayer* layer) { m_simulationLayer = layer; }
        void setDebugPerceptionLayer(Layer* layer) { m_debugPerceptionLayer = layer; }
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

        // Start/stop trajectory+metrics export (the Engine owns the exporters).
        using ExportToggleCallback = std::function<void(bool)>;
        void setExportToggleCallback(ExportToggleCallback cb) { m_exportToggleCallback = std::move(cb); }
        void setExportActive(bool active) { m_exportEnabled = active; } // reflect engine state

      private:
        SDL_Window* m_window;
        SDL_Renderer* m_renderer;
        Simulation* m_simulation;
        SimulationLayer* m_simulationLayer{nullptr};
        Layer* m_debugPerceptionLayer{nullptr};
        AlertManager* m_alertManager{nullptr};
        RecordingManager* m_recordingManager{nullptr};

        // Vehicle inspector (Phase 7 observability): shows the selected vehicle's
        // observation/action/sectors/violations from Simulation::inspect().
        void renderVehicleInspector();

        bool m_initialized{false};
        bool m_showKeybindings{false};
        bool m_showInspector{false};
        bool m_exportEnabled{false};
        ExportToggleCallback m_exportToggleCallback;

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