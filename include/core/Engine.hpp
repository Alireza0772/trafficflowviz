#ifndef TFV_ENGINE_HPP
#define TFV_ENGINE_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <optional>
#include <filesystem>

#include "alerts/AlertManager.hpp"
#include "core/Configuration.hpp"
#include "core/Event.hpp"
#include "core/Events/KeyEvent.hpp"
#include "core/Events/WindowEvent.hpp"
#include "core/LayerStack.hpp"
#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "core/Window.hpp"
#include "network/LiveFeed.hpp"
#include "recording/RecordingManager.hpp"
#include "rendering/Renderer.hpp"
#include "rendering/layers/DebugPerceptionLayer.hpp"
#include "rendering/layers/HeatmapLayer.hpp"
#include "rendering/layers/ImGuiLayer.hpp"
#include "rendering/layers/SimulationLayer.hpp"

namespace tfv
{

    // Alert callback
    using AlertUICallback = std::function<void(const std::string& message, uint32_t segmentId)>;

    class Engine
    {
      public:
        Engine();
        ~Engine();

        /** Initialize with configuration */
        bool initialize(int argc = 0, char* argv[] = nullptr, 
                       const std::optional<std::filesystem::path>& configFile = std::nullopt);

        /** Override default CSV path before init(). */
        void setCityInfo(std::string p) { m_cityInfoPath = std::move(p); }

        /** Set the vehicle CSV path */
        void setVehicleInfo(std::string p) { m_vehicleInfoPath = std::move(p); }

        bool init();
        void run();

        // Feature toggles
        void toggleHeatmap(bool enable);
        void toggleRecording(bool enable);
        void toggleLiveFeed(bool enable);
        void toggleAlerts(bool enable);
        void toggleImGui(bool enable);
        void toggleAntiAliasing(bool enable);
        void toggleKeybindingsWindow(bool enable);

        // Export functionality
        bool exportImage(const std::string& path);
        bool startVideoRecording(const std::string& path, int fps = 30);
        bool stopVideoRecording();

        // Live feed control
        bool connectToFeed(const std::string& url, FeedType type = FeedType::WEBSOCKET);
        bool disconnectFromFeed();

        // UI callback for alerts
        void setAlertCallback(AlertUICallback cb);

      private:
        void handleEvents();
        void onEvent(Event& e);
        void update(double dt);
        void render();
        void processAlert(AlertType type, uint32_t segmentId, const std::string& message);

        // Helper methods
        bool updateFPSCounter(double dt);

        // window / renderer
        std::string m_title;
        int m_w, m_h;
        std::unique_ptr<Window> m_window;
        std::string m_rendererType;
        Renderer* m_renderer{nullptr};

        // timing
        bool m_running{false};
        bool m_initialized{false};
        double m_t{0.0};
        double m_fpsTimer{0.0};
        int m_frameCount{0};
        int m_fps{0};

        // fixed-timestep accumulator (deterministic simulation stepping)
        double m_fixedDt{1.0 / 60.0};
        double m_accumulator{0.0};
        double m_maxFrameDt{0.25};
        uint64_t m_tick{0};

        // core subsystems
        Simulation m_sim;
        RoadNetwork m_roads;
        std::unique_ptr<LiveFeed> m_liveFeed;
        std::unique_ptr<AlertManager> m_alertManager;
        std::unique_ptr<RecordingManager> m_recordingManager;

        // Layers
        LayerStack m_layerStack;
        std::shared_ptr<SimulationLayer> m_simulationLayer;
        std::shared_ptr<HeatmapLayer> m_heatmapLayer;
        std::shared_ptr<DebugPerceptionLayer> m_debugPerceptionLayer;
        std::shared_ptr<ImGuiLayer> m_imguiLayer;

        // data paths (set by configuration)
        std::string m_vehicleInfoPath;
        std::string m_cityInfoPath;

        // Feature flags
        bool m_showHeatmap{false};
        bool m_recordingEnabled{false};
        bool m_alertsEnabled{false};
        bool m_liveFeedEnabled{false};
        bool m_imguiEnabled{true};        // ImGui enabled by default
        bool m_antiAliasingEnabled{true}; // Anti-aliasing enabled by default
        bool m_showKeybindings{false};

        // UI callback for alerts
        AlertUICallback m_alertUICallback;
    };

} // namespace tfv
#endif