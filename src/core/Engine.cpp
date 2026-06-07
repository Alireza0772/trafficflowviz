#include "core/Engine.hpp"

#include <chrono>
#include <cmath>
#include <iostream>

#include "alerts/AlertManager.hpp"
#include "data/CSVLoader.hpp"
#include "io/MetricsExporter.hpp"
#include "io/RunManifest.hpp"
#include "io/TrajectoryExporter.hpp"
#include "recording/RecordingManager.hpp"
#include "rendering/layers/HeatmapLayer.hpp"
#include "rendering/layers/ImGuiLayer.hpp"
#include "rendering/layers/SimulationLayer.hpp"
#include "utils/LoggingManager.hpp"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <imgui.h>
#include <vector>

// Include SDL only for event handling - will be abstracted in future updates
#include <SDL2/SDL.h>

#ifndef TFV_GIT_SHA
#define TFV_GIT_SHA "unknown"
#endif

namespace tfv
{
    namespace
    {
        // FNV-1a over the final state — BYTE-IDENTICAL to headless_runner.cpp's
        // stateDigest(), so a GUI-exported run's manifest digest matches a headless one.
        uint64_t exportStateDigest(const VehicleMap& snap)
        {
            std::vector<uint64_t> ids;
            for(const auto& [id, v] : snap)
            {
                (void)v;
                ids.push_back(id);
            }
            std::sort(ids.begin(), ids.end());
            uint64_t h = 1469598103934665603ULL;
            auto mix = [&](uint64_t x) {
                h ^= x;
                h *= 1099511628211ULL;
            };
            for(uint64_t id : ids)
            {
                const Vehicle& v = snap.at(id);
                const float sp = glm::length(v.vel);
                mix(id);
                mix(v.segmentId);
                mix(static_cast<uint64_t>(v.laneIndex));
                mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(v.position * 1e6))));
                mix(static_cast<uint64_t>(static_cast<int64_t>(std::llround(sp * 1e3))));
            }
            return h;
        }
    } // namespace

    Engine::Engine()
    {
    }

    Engine::~Engine()
    {
        // Flush a final manifest if a run was still exporting at shutdown.
        if(m_exportEnabled)
            toggleExport(false);

        // Clean up in reverse order of creation
        m_layerStack.clear();
        m_simulationLayer.reset();
        m_heatmapLayer.reset();
        m_imguiLayer.reset();

        m_recordingManager.reset();
        m_alertManager.reset();
        m_liveFeed.reset();

        if(m_renderer)
            delete m_renderer;

        // Window cleanup is handled by the Window abstraction's destructor
        if(m_window)
            m_window->shutdown();
    }

    bool Engine::initialize(int argc, char* argv[], 
                           const std::optional<std::filesystem::path>& configFile)
    {
        // Initialize configuration system first
        if (!TFV_CONFIG().initialize(configFile, argc, argv)) {
            LOG_ERROR("Failed to initialize configuration system");
            return false;
        }

        // Set up application parameters from configuration
        m_title = TFV_CONFIG().getApplicationTitle();
        m_w = TFV_CONFIG().getWindowWidth();
        m_h = TFV_CONFIG().getWindowHeight();
        m_rendererType = TFV_CONFIG().getRendererType();

        // Set data paths from configuration
        m_vehicleInfoPath = TFV_CONFIG().getVehicleDataPath().string();
        m_cityInfoPath = TFV_CONFIG().getCityDataPath().string();

        // Set feature flags from configuration
        m_showHeatmap = TFV_CONFIG().isHeatmapEnabled();
        m_alertsEnabled = TFV_CONFIG().isAlertsEnabled();
        m_recordingEnabled = TFV_CONFIG().isRecordingEnabled();

        // Fixed simulation timestep (deterministic stepping, decoupled from FPS)
        float ts = TFV_CONFIG().getSimulationTimeStep();
        if(ts > 0.0f)
            m_fixedDt = static_cast<double>(ts);

        return init();
    }

    bool Engine::init()
    {
        if(m_initialized)
            return true; // idempotent: safe whether called via initialize() or run()

        // Create window using the abstraction
        WindowProps props;
        props.title = m_title;
        props.width = m_w;
        props.height = m_h;
        
        m_window = Window::create(props);
        if(!m_window || !m_window->initialize(props))
        {
            LOG_ERROR("Window creation failed");
            return false;
        }
        
        // Set up event callback for proper event handling
        m_window->setEventCallback([this](Event& e) {
            this->onEvent(e);
        });

        // Create the renderer using factory method
        m_renderer = Renderer::create(m_rendererType, m_window->getNativeWindow()).release();
        if(!m_renderer)
        {
            LOG_ERROR("Renderer creation failed");
            return false;
        }
        LOG_INFO("Renderer created successfully");
        if(!m_renderer->initialize())
        {
            LOG_ERROR("Renderer initialization failed");
            return false;
        }
        LOG_INFO("Renderer initialized successfully");

        if(!m_sim.initialize(m_cityInfoPath, m_vehicleInfoPath))
        {
            LOG_ERROR("Simulation initialization failed");
            return false;
        }
        LOG_INFO("Simulation initialized successfully");

        // Initialize alert manager
        m_alertManager = std::make_unique<AlertManager>(m_sim);
        m_alertManager->setAlertCallback(
            [this](const Alert& alert)
            {
                // Handle new alerts
                if(m_alertUICallback)
                {
                    m_alertUICallback(alert.message, alert.segmentId);
                }
            });

        // Initialize recording manager
        m_recordingManager = std::make_unique<RecordingManager>(m_renderer);
        m_recordingManager->setStatusCallback([](const std::string& msg)
                                              { LOG_INFO("[Recording] {msg}", PARAM(msg, msg)); });

        // Create and initialize layers

        // 1. Simulation layer (base layer)
        m_simulationLayer = std::make_shared<SimulationLayer>(m_renderer, &m_sim);
        m_layerStack.pushLayer(m_simulationLayer);

        // 2. Heatmap layer
        m_heatmapLayer =
            std::make_shared<HeatmapLayer>(m_renderer, &m_sim, m_simulationLayer.get());
        m_layerStack.pushLayer(m_heatmapLayer);
        m_heatmapLayer->setEnabled(m_showHeatmap);

        // 2b. Perception debug overlay (off by default; toggled from the View menu)
        m_debugPerceptionLayer =
            std::make_shared<DebugPerceptionLayer>(m_renderer, &m_sim, m_simulationLayer.get());
        m_layerStack.pushLayer(m_debugPerceptionLayer);

        // 3. ImGui layer (top layer)
        if(m_rendererType == "SDL")
        {
            void* nativeRenderer = m_renderer->getNativeRenderer();
            m_imguiLayer =
                std::make_shared<ImGuiLayer>(static_cast<SDL_Window*>(m_window->getNativeWindow()),
                                             static_cast<SDL_Renderer*>(nativeRenderer), &m_sim);

            m_imguiLayer->setSimulationLayer(m_simulationLayer.get());
            m_imguiLayer->setDebugPerceptionLayer(m_debugPerceptionLayer.get());
            m_imguiLayer->setAlertManager(m_alertManager.get());
            m_imguiLayer->setRecordingManager(m_recordingManager.get());
            m_imguiLayer->showKeybindingsWindow(m_showKeybindings);
            m_imguiLayer->setExportToggleCallback([this](bool e) {
                toggleExport(e);
                if(m_imguiLayer) // keep the menu label in sync even if start failed-soft
                    m_imguiLayer->setExportActive(m_exportEnabled);
            });

            m_layerStack.pushLayer(m_imguiLayer);
            m_imguiLayer->setEnabled(m_imguiEnabled);
        }

        // Optional: auto-start export for the whole GUI run (config-driven; default off).
        m_exportEveryN = std::max(1, TFV_CONFIG().getInt("sim.export.every_n", 1));
        if(TFV_CONFIG().getInt("sim.export.enabled", 0) != 0)
        {
            toggleExport(true);
            if(m_imguiLayer)
                m_imguiLayer->setExportActive(m_exportEnabled);
        }

        m_initialized = true;
        return true;
    }

    void Engine::run()
    {
        if(!init())
            return;

        using clk = std::chrono::high_resolution_clock;
        auto last = clk::now();
        m_running = true;

        while(m_running)
        {
            auto now = clk::now();
            double dt = std::chrono::duration<double>(now - last).count();
            last = now;

            handleEvents();
            update(dt);
            render();

            // Capture frame if recording
            if(m_recordingEnabled && m_recordingManager && m_recordingManager->isRecording())
            {
                m_recordingManager->captureFrame();
            }

            // Update FPS counter
            updateFPSCounter(dt);
        }
    }

    void Engine::handleEvents()
    {
        // Use window abstraction to poll events
        m_window->pollEvents();
    }

    void Engine::onEvent(Event& e)
    {
        // Process events through the layer stack first
        if(m_layerStack.onEvent(e))
        {
            // Event was handled by a layer
            return;
        }

        // Handle application-level events
        if(e.getEventType() == EventType::WindowClose)
        {
            m_running = false;
            return;
        }

        if(e.getEventType() == EventType::KeyPressed)
        {
            KeyPressedEvent& keyEvent = static_cast<KeyPressedEvent&>(e);
            switch(keyEvent.getKeyCode())
            {
            case SDLK_ESCAPE:
                m_running = false;
                break;

            // Feature toggles
            case SDLK_h: // Toggle heatmap
                toggleHeatmap(!m_showHeatmap);
                break;
            case SDLK_l: // Toggle live feed
                toggleLiveFeed(!m_liveFeedEnabled);
                break;
            case SDLK_a: // Toggle alerts
                toggleAlerts(!m_alertsEnabled);
                break;
            case SDLK_r: // Toggle recording
                toggleRecording(!m_recordingEnabled);
                break;
            case SDLK_i: // Toggle ImGui
                toggleImGui(!m_imguiEnabled);
                break;
            case SDLK_g: // Toggle anti-aliasing
                toggleAntiAliasing(!m_antiAliasingEnabled);
                break;
            case SDLK_k: // Toggle keybindings window
                toggleKeybindingsWindow(!m_showKeybindings);
                break;

            // Export functions
            case SDLK_s: // Save screenshot
                if(m_recordingManager)
                {
                    m_recordingManager->captureScreenshot(
                        "trafficviz_" + std::to_string(static_cast<long>(time(nullptr))) +
                        ".png");
                }
                break;

            default:
                break;
            }
        }
    }

    void Engine::update(double dt)
    {
        // Fixed-timestep accumulator: advance the simulation in deterministic
        // quanta (m_fixedDt) independent of the render frame rate. dt is clamped
        // to m_maxFrameDt to avoid the "spiral of death" if a frame stalls
        // (e.g. paused under a debugger or a slow first frame).
        m_accumulator += (dt < m_maxFrameDt ? dt : m_maxFrameDt);
        while(m_accumulator >= m_fixedDt)
        {
            m_sim.update(m_fixedDt);
            m_accumulator -= m_fixedDt;
            ++m_tick;
            // Trajectory/metrics export (read-only; only when active). Sample AFTER the
            // sim step with a 0-based tick RELATIVE to export start (et=0,1,2,...), so a
            // from-start GUI export byte-matches the headless runner's cadence + every_n
            // decimation phase (which also uses 0-based t).
            if(m_exportEnabled && m_trajExporter)
            {
                const uint64_t et = (m_tick - 1) - m_exportStartTick;
                const double simTime = static_cast<double>(et) * m_fixedDt;
                m_trajExporter->sample(et, simTime, m_sim);
                m_metricsExporter->sample(et, simTime, m_sim);
            }
        }

        // LiveFeed ingest (Phase 6) and AlertManager update hooks are not yet
        // implemented; intentionally no-ops here.

        // Update all layers once per rendered frame (render-side animation only).
        m_layerStack.onUpdate(dt);

        m_t += dt;
    }

    void Engine::toggleExport(bool enable)
    {
        if(enable == m_exportEnabled)
            return; // no-op on double-start / stop-without-start
        if(enable)
        {
            std::time_t t = std::time(nullptr);
            char ts[32];
            std::strftime(ts, sizeof(ts), "run_%Y%m%d_%H%M%S", std::gmtime(&t));
            const std::string base = TFV_CONFIG().getString("export.dir", "output");
            m_exportDir = (std::filesystem::path(base) / ts).string();
            std::error_code ec;
            std::filesystem::create_directories(m_exportDir, ec);
            if(ec)
            {
                LOG_ERROR("[export] cannot create {dir}: {e}", PARAM(dir, m_exportDir),
                          PARAM(e, ec.message()));
                m_exportDir.clear();
                return; // fail-soft: stay disabled
            }
            m_trajExporter = std::make_unique<TrajectoryExporter>(
                (std::filesystem::path(m_exportDir) / "trajectory.csv").string(), m_exportEveryN);
            m_metricsExporter = std::make_unique<MetricsExporter>(
                (std::filesystem::path(m_exportDir) / "metrics.csv").string(), m_exportEveryN);
            if(!m_trajExporter->ok() || !m_metricsExporter->ok())
            {
                LOG_ERROR("[export] could not open output files in {dir}", PARAM(dir, m_exportDir));
                m_trajExporter.reset();
                m_metricsExporter.reset();
                m_exportDir.clear();
                return;
            }
            m_exportStartTick = m_tick;
            m_exportEnabled = true;
            LOG_INFO("[export] started -> {dir}", PARAM(dir, m_exportDir));
        }
        else
        {
            writeExportManifest();
            m_trajExporter.reset();
            m_metricsExporter.reset();
            m_exportEnabled = false;
            LOG_INFO("[export] stopped ({dir})", PARAM(dir, m_exportDir));
        }
    }

    void Engine::writeExportManifest()
    {
        if(m_exportDir.empty())
            return;
        auto& cfg = TFV_CONFIG();
        RunManifest m;
        m.masterSeed = cfg.getMasterSeed();
        m.brainKind = m_sim.brainKind();
        m.brainWeightsHash = m_sim.brainWeightsHash();
        m.cityFile = cfg.getString("data.city_file", "");
        m.vehiclesFile = cfg.getString("data.vehicles_file", "");
        m.signsFile = cfg.getString("data.signs_file", "");
        m.vehicleCount = m_sim.vehicleCount();
        m.decisionHz = cfg.getFloat("perf.decision_hz", 10.0f);
        m.fixedDt = m_fixedDt;
        m.totalTicks = m_tick - m_exportStartTick;
        m.exportEveryN = m_exportEveryN;
        m.gitSha = TFV_GIT_SHA;
        char ts[32];
        std::time_t tt = std::time(nullptr);
        std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&tt));
        m.wallClockUtc = ts; // never hashed
        m.finalStateDigest = exportStateDigest(m_sim.snapshot());
        for(const auto& [k, v] : cfg.snapshot("sim."))
            m.configSnapshot[k] = v;
        for(const auto& [k, v] : cfg.snapshot("perf."))
            m.configSnapshot[k] = v;
        std::ofstream mf((std::filesystem::path(m_exportDir) / "manifest.json").string());
        m.write(mf);
    }

    void Engine::render()
    {
        // Background follows the active scene theme (studio dark / paper light).
        const RGB8 bg = m_simulationLayer ? m_simulationLayer->backgroundColor() : RGB8{24, 26, 32};
        m_renderer->clear(bg.r, bg.g, bg.b, 255);

        // Render all layers
        m_layerStack.onRender();

        // Render ImGui components if enabled
        if(m_imguiEnabled)
        {
            m_layerStack.onImGuiRender();
        }

        // Present the renderer
        m_renderer->present();
    }

    bool Engine::updateFPSCounter(double dt)
    {
        m_fpsTimer += dt;
        m_frameCount++;

        if(m_fpsTimer >= 1.0)
        {
            m_fps = m_frameCount;
            m_frameCount = 0;
            m_fpsTimer = 0.0;

            // Update ImGui layer with new FPS if available
            if(m_imguiLayer)
            {
                // Set fps directly on layer or provide a setter method
                // in the ImGuiLayer class instead of directly accessing private member
                m_imguiLayer->setFPS(m_fps);
            }

            return true;
        }
        return false;
    }

    void Engine::toggleHeatmap(bool enable)
    {
        m_showHeatmap = enable;
        if(m_heatmapLayer)
        {
            m_heatmapLayer->setEnabled(enable);
        }
    }

    void Engine::toggleRecording(bool enable)
    {
        m_recordingEnabled = enable;

        if(m_recordingManager)
        {
            if(enable && !m_recordingManager->isRecording())
            {
                // Start recording
                std::string filename =
                    "trafficviz_" + std::to_string(static_cast<long>(time(nullptr))) + ".mp4";
                m_recordingManager->startRecording(filename, 30);
            }
            else if(!enable && m_recordingManager->isRecording())
            {
                // Stop recording
                m_recordingManager->stopRecording();
            }
        }
    }

    void Engine::toggleLiveFeed(bool enable)
    {
        m_liveFeedEnabled = enable;
        // Additional implementation if needed
    }

    void Engine::toggleAlerts(bool enable)
    {
        m_alertsEnabled = enable;
        if(m_alertManager)
        {
            // Enable/disable the alert manager
            // m_alertManager->setEnabled(enable);
        }
    }

    bool Engine::exportImage(const std::string& path)
    {
        if(m_recordingManager)
        {
            return m_recordingManager->captureScreenshot(path);
        }
        return false;
    }

    bool Engine::startVideoRecording(const std::string& path, int fps)
    {
        if(m_recordingManager)
        {
            bool success = m_recordingManager->startRecording(path, fps);
            if(success)
            {
                m_recordingEnabled = true;
            }
            return success;
        }
        return false;
    }

    bool Engine::stopVideoRecording()
    {
        if(m_recordingManager)
        {
            bool success = m_recordingManager->stopRecording();
            if(success)
            {
                m_recordingEnabled = false;
            }
            return success;
        }
        return false;
    }

    bool Engine::connectToFeed(const std::string& url, FeedType type)
    {
        // Implementation of live feed connection
        return false;
    }

    bool Engine::disconnectFromFeed()
    {
        // Implementation of live feed disconnection
        return false;
    }

    void Engine::setAlertCallback(AlertUICallback cb)
    {
        m_alertUICallback = cb;
    }

    void Engine::processAlert(AlertType type, uint32_t segmentId, const std::string& message)
    {
        // Implementation of alert processing
    }

    void Engine::toggleKeybindingsWindow(bool enable)
    {
        m_showKeybindings = enable;
        if(m_imguiLayer)
        {
            m_imguiLayer->showKeybindingsWindow(enable);
        }
    }

    void Engine::toggleImGui(bool enable)
    {
        m_imguiEnabled = enable;
        if(m_imguiLayer)
        {
            m_imguiLayer->setEnabled(enable);
        }
    }

    void Engine::toggleAntiAliasing(bool enable)
    {
        m_antiAliasingEnabled = enable;
        if(m_renderer)
        {
            m_renderer->setAntiAliasing(enable);
        }
    }

} // namespace tfv