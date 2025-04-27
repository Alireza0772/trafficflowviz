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

    Engine::Engine(const std::string& title, int w, int h, const std::string& rendererType)
        : m_title(title), m_w(w), m_h(h), m_rendererType(rendererType)
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

        // Clean up the SDL window if using SDL
        if(m_window && m_rendererType == "SDL")
            SDL_DestroyWindow(static_cast<SDL_Window*>(m_window));

        // Quit SDL if using SDL renderer
        if(m_rendererType == "SDL")
            SDL_Quit();
    }

    bool Engine::init()
    {
        // Initialize SDL for the SDL renderer
        if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        {
            LOG_ERROR("SDL init failed: {error}", PARAM(error, SDL_GetError()));
            return false;
        }

        // Create window with no border, resizable, and with a specific size also without top client
        // area to make it look like a native window
        m_window = SDL_CreateWindow(m_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    m_w, m_h, SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE);

        if(!m_window)
        {
            LOG_ERROR("Window creation failed: {error}", PARAM(error, SDL_GetError()));
            return false;
        }
        LOG_INFO("Window created successfully");
        // Set window icon
        SDL_Surface* icon = SDL_LoadBMP("assets/icon.bmp");

        if(icon)
        {
            SDL_SetWindowIcon(static_cast<SDL_Window*>(m_window), icon);
            SDL_FreeSurface(icon);
        }
        else
        {
            LOG_ERROR("Failed to load window icon: {error}", PARAM(error, SDL_GetError()));
        }

        // Create the renderer using factory method
        m_renderer = Renderer::create(m_rendererType, m_window).release();
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
        m_recordingManager->setStatusCallback([this](const std::string& msg)
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
                std::make_shared<ImGuiLayer>(static_cast<SDL_Window*>(m_window),
                                             static_cast<SDL_Renderer*>(nativeRenderer), &m_sim);

            m_imguiLayer->setSimulationLayer(m_simulationLayer.get());
            m_imguiLayer->setAlertManager(m_alertManager.get());
            m_imguiLayer->setRecordingManager(m_recordingManager.get());
            m_imguiLayer->showKeybindingsWindow(m_showKeybindings);

            m_layerStack.pushLayer(m_imguiLayer);
            m_imguiLayer->setEnabled(m_imguiEnabled);
        }

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
        // For now, keep SDL event handling since we're focusing on rendering API abstraction
        SDL_Event e;
        while(SDL_PollEvent(&e))
        {
            // Process events through the layer stack first
            if(m_layerStack.onEvent(&e))
            {
                // Event was handled by a layer
                continue;
            }

            // Handle application-level events
            if(e.type == SDL_QUIT)
                m_running = false;

            if(e.type == SDL_KEYDOWN)
            {
                switch(e.key.keysym.sym)
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
    }

    void Engine::update(double dt)
    {
        // Update simulation
        m_sim.update(dt);

        // Process live data if enabled
        if(m_liveFeedEnabled && m_liveFeed)
        {
            // Note: These should be implemented in LiveFeed class if needed
            // For now, removed these calls since they don't exist
            // m_liveFeed->update();
            // const auto& updates = m_liveFeed->getVehicleUpdates();
            // for(const auto& update : updates)
            // {
            //     m_sim.updateVehicle(update);
            // }
        }

        // Process alerts if enabled
        if(m_alertsEnabled && m_alertManager)
        {
            // This should be implemented in AlertManager if needed
            // m_alertManager->update(dt);
        }

        // Update all layers
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