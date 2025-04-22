#ifndef TFV_ENGINE_HPP
#define TFV_ENGINE_HPP

#include <SDL2/SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "network/LiveFeed.hpp"
#include "rendering/Renderer.hpp"
#include "rendering/SceneRenderer.hpp"

namespace tfv
{
    // Forward declarations
    class HeatmapRenderer;
    class AlertManager;
    class RecordingManager;
    class ImGuiRenderer;

    // Alert callback
    using AlertUICallback = std::function<void(const std::string& message, uint32_t segmentId)>;

    class Engine
    {
      public:
        Engine(const std::string& title, int w, int h);
        ~Engine();

        /** Override default CSV path before init(). */
        void setCSV(std::string p) { m_csvPath = std::move(p); }

        /** Set the roads CSV path */
        void setRoadCSV(std::string p) { m_roadPath = std::move(p); }

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
        SDL_Window* m_window{nullptr};
        tfv::IRenderer* m_renderer{nullptr};

        // timing
        bool m_running{false};
        double m_t{0.0};
        double m_fpsTimer{0.0};
        int m_frameCount{0};
        int m_fps{0};

        // core subsystems
        Simulation m_sim;
        SceneRenderer* m_scene{nullptr};
        RoadNetwork m_roads;
        std::unique_ptr<LiveFeed> m_liveFeed;
        std::unique_ptr<HeatmapRenderer> m_heatmap;
        std::unique_ptr<AlertManager> m_alertManager;
        std::unique_ptr<RecordingManager> m_recordingManager;
        std::unique_ptr<ImGuiRenderer> m_imguiRenderer;

        // data paths
        std::string m_csvPath{"./data/vehicles/vehicles.csv"};
        std::string m_roadPath{"./data/roads/roads_complex.csv"};

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

        // ImGui helper functions
        static void HelpMarker(const char* desc);
    };

} // namespace tfv
#endif