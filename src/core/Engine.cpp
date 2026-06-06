#include "core/Engine.hpp"

#include <chrono>
#include <cmath>
#include <iostream>

#include "alerts/AlertManager.hpp"
#include "data/CSVLoader.hpp"
#include "recording/RecordingManager.hpp"
#include "rendering/layers/HeatmapLayer.hpp"
#include "rendering/layers/ImGuiLayer.hpp"
#include "rendering/layers/SimulationLayer.hpp"
#include "utils/LoggingManager.hpp"
#include <imgui.h>

// Include SDL only for event handling - will be abstracted in future updates
#include <SDL2/SDL.h>

namespace tfv
{

    Engine::Engine()
    {
    }

    Engine::~Engine()
    {
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

        // 3. ImGui layer (top layer)
        if(m_rendererType == "SDL")
        {
            void* nativeRenderer = m_renderer->getNativeRenderer();
            m_imguiLayer =
                std::make_shared<ImGuiLayer>(static_cast<SDL_Window*>(m_window->getNativeWindow()),
                                             static_cast<SDL_Renderer*>(nativeRenderer), &m_sim);

            m_imguiLayer->setSimulationLayer(m_simulationLayer.get());
            m_imguiLayer->setAlertManager(m_alertManager.get());
            m_imguiLayer->setRecordingManager(m_recordingManager.get());
            m_imguiLayer->showKeybindingsWindow(m_showKeybindings);

            m_layerStack.pushLayer(m_imguiLayer);
            m_imguiLayer->setEnabled(m_imguiEnabled);
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
        }

        // LiveFeed ingest (Phase 6) and AlertManager update hooks are not yet
        // implemented; intentionally no-ops here.

        // Update all layers once per rendered frame (render-side animation only).
        m_layerStack.onUpdate(dt);

        m_t += dt;
    }

    void Engine::render()
    {
        m_renderer->clear(0, 0, 0, 255);

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