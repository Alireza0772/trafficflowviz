#ifndef TFV_ENGINE_HPP
#define TFV_ENGINE_HPP

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "alerts/AlertManager.hpp"
#include "core/LayerStack.hpp"
#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "network/LiveFeed.hpp"
#include "recording/RecordingManager.hpp"
#include "rendering/Renderer.hpp"
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
        Engine(const std::string& title, int w, int h, const std::string& rendererType = "SDL");
        ~Engine();

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
        void update(double dt);
        void render();
        void processAlert(AlertType type, uint32_t segmentId, const std::string& message);

        // Helper methods
        bool updateFPSCounter(double dt);

        // window / renderer
        std::string m_title;
        int m_w, m_h;
        void* m_window{nullptr};
        std::string m_rendererType;
        Renderer* m_renderer{nullptr};

        // timing
        bool m_running{false};
        double m_t{0.0};
        double m_fpsTimer{0.0};
        int m_frameCount{0};
        int m_fps{0};

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
        std::shared_ptr<ImGuiLayer> m_imguiLayer;

        // data paths
        std::string m_vehicleInfoPath{"./data/vehicles/vehicles.csv"};
        std::string m_cityInfoPath{"./data/roads/roads_complex.csv"};

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