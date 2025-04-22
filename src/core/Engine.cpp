#include "core/Engine.hpp"

#include <chrono>
#include <cmath>
#include <iostream>

#include "alerts/AlertManager.hpp"
#include "data/CSVLoader.hpp"
#include "recording/RecordingManager.hpp"
#include "rendering/HeatmapRenderer.hpp"
#include "rendering/ImGuiRenderer.hpp"
#include <imgui.h>

namespace tfv
{

    Engine::Engine(const std::string& title, int w, int h) : m_title(title), m_w(w), m_h(h) {}

    Engine::~Engine()
    {
        // Clean up in reverse order of creation
        m_imguiRenderer.reset();
        m_recordingManager.reset();
        m_alertManager.reset();
        m_liveFeed.reset();
        m_heatmap.reset();

        if(m_scene)
            delete m_scene;
        if(m_renderer)
            delete m_renderer;
        if(m_window)
            SDL_DestroyWindow(m_window);
        SDL_Quit();
    }

    bool Engine::init()
    {
        if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        {
            std::cerr << "SDL init failed: " << SDL_GetError() << '\n';
            return false;
        }

        m_window = SDL_CreateWindow(m_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    m_w, m_h, 0);

        if(!m_window)
        {
            std::cerr << "Window creation failed: " << SDL_GetError() << '\n';
            return false;
        }

        SDL_Renderer* sdlRenderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
        if(!sdlRenderer)
        {
            std::cerr << "Renderer creation failed: " << SDL_GetError() << '\n';
            return false;
        }

        m_renderer = new SDLRenderer(sdlRenderer);
        m_scene = new SceneRenderer(m_renderer);

        // Initialize ImGui
        m_imguiRenderer = std::make_unique<ImGuiRenderer>(m_window, sdlRenderer);
        m_imguiRenderer->init();

        // Enable keybindings window by default
        m_showKeybindings = true;

        // Load road network
        m_roads.loadCSV(m_roadPath);
        m_scene->setNetwork(&m_roads);

        // Connect road network to simulation
        m_sim.setRoadNetwork(&m_roads);

        // Load vehicles
        auto vehicles = tfv::loadVehiclesCSV(m_csvPath);
        std::cout << "[CSV] loaded " << vehicles.size() << " vehicles\n";
        for(const auto& v : vehicles)
            m_sim.addVehicle(v);

        // Initialize heatmap renderer
        m_heatmap = std::make_unique<HeatmapRenderer>(m_renderer);

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
        SDL_Renderer* rawRenderer = dynamic_cast<SDLRenderer*>(m_renderer)->getSDLRenderer();
        m_recordingManager = std::make_unique<RecordingManager>(rawRenderer, m_w, m_h);
        m_recordingManager->setStatusCallback([](const std::string& msg)
                                              { std::cout << "[Recording] " << msg << std::endl; });

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
        SDL_Event e;
        while(SDL_PollEvent(&e))
        {
            // Process ImGui events first if enabled
            if(m_imguiEnabled && m_imguiRenderer)
            {
                m_imguiRenderer->processEvent(e);
            }

            if(e.type == SDL_QUIT)
                m_running = false;

            if(e.type == SDL_KEYDOWN)
            {
                switch(e.key.keysym.sym)
                {
                case SDLK_ESCAPE:
                    m_running = false;
                    break;

                // Pan with arrow keys
                case SDLK_LEFT:
                    m_scene->setPan(m_scene->getPanX() - 20, m_scene->getPanY());
                    break;
                case SDLK_RIGHT:
                    m_scene->setPan(m_scene->getPanX() + 20, m_scene->getPanY());
                    break;
                case SDLK_UP:
                    m_scene->setPan(m_scene->getPanX(), m_scene->getPanY() - 20);
                    break;
                case SDLK_DOWN:
                    m_scene->setPan(m_scene->getPanX(), m_scene->getPanY() + 20);
                    break;

                // Zoom controls
                case SDLK_EQUALS: // '+' key (zoom in)
                case SDLK_KP_PLUS:
                    m_scene->setZoom(m_scene->getZoom() * 1.1f);
                    break;
                case SDLK_MINUS: // '-' key (zoom out)
                case SDLK_KP_MINUS:
                    m_scene->setZoom(m_scene->getZoom() / 1.1f);
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

            // Zoom with mouse wheel
            if(e.type == SDL_MOUSEWHEEL)
            {
                float zoom = m_scene->getZoom();
                if(e.wheel.y > 0)
                    m_scene->setZoom(zoom * 1.1f);
                else if(e.wheel.y < 0)
                    m_scene->setZoom(zoom / 1.1f);
            }
        }
    }

    void Engine::update(double dt)
    {
        // Update simulation
        m_sim.step(dt);

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

        // Update scene
        m_scene->update(dt);

        m_t += dt;
    }

    void Engine::render()
    {
        m_renderer->clear(0, 0, 0, 255);

        // Get current vehicle snapshot from simulation
        VehicleMap vehicles = m_sim.snapshot();

        // Draw scene contents with the latest vehicle data
        m_scene->draw(vehicles);

        // Render heatmap if enabled
        if(m_showHeatmap && m_heatmap)
        {
            // Use existing draw method instead of render
            m_heatmap->draw(m_scene->getNetwork(), m_sim.getCongestionLevels(), m_scene->getPanX(),
                            m_scene->getPanY(), m_scene->getZoom());
        }

        // Render ImGui if enabled
        if(m_imguiEnabled && m_imguiRenderer)
        {
            m_imguiRenderer->beginFrame();

            // Show keybindings window by default
            m_showKeybindings = true;
            m_imguiRenderer->showKeybindingsWindow(&m_showKeybindings);

            // Add any additional ImGui UI here
            // FPS display
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(170, 70), ImGuiCond_FirstUseEver);
            ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("FPS: %d", m_fps);
            ImGui::Text("Vehicle count: %zu", vehicles.size());
            ImGui::End();

            m_imguiRenderer->endFrame();
        }

        m_renderer->present();
    }

    bool Engine::updateFPSCounter(double dt)
    {
        m_frameCount++;
        m_fpsTimer += dt;

        if(m_fpsTimer >= 1.0)
        {
            m_fps = m_frameCount;
            m_frameCount = 0;
            m_fpsTimer -= 1.0;
            return true;
        }

        return false;
    }

    void Engine::toggleHeatmap(bool enable)
    {
        m_showHeatmap = enable;
        std::cout << "Heatmap: " << (enable ? "enabled" : "disabled") << std::endl;
    }

    void Engine::toggleRecording(bool enable)
    {
        if(!m_recordingManager)
            return;

        if(enable && !m_recordingEnabled)
        {
            // Start recording
            std::string filename =
                "trafficviz_" + std::to_string(static_cast<long>(time(nullptr))) + ".mp4";
            if(m_recordingManager->startRecording(filename))
            {
                m_recordingEnabled = true;
                std::cout << "Recording started: " << filename << std::endl;
            }
        }
        else if(!enable && m_recordingEnabled)
        {
            // Stop recording
            if(m_recordingManager->stopRecording())
            {
                m_recordingEnabled = false;
                std::cout << "Recording stopped" << std::endl;
            }
        }
    }

    void Engine::toggleLiveFeed(bool enable)
    {
        if(enable && !m_liveFeedEnabled)
        {
            // Create and start live feed
            if(!m_liveFeed)
            {
                m_liveFeed = std::make_unique<LiveFeed>(m_sim);
            }

            // Connect to feed (use dummy for now)
            m_liveFeed->connect("ws://localhost:8080", FeedType::DUMMY);
            m_liveFeedEnabled = true;
            std::cout << "Live feed: enabled" << std::endl;
        }
        else if(!enable && m_liveFeedEnabled)
        {
            // Disconnect from feed
            if(m_liveFeed)
            {
                m_liveFeed->disconnect();
                m_liveFeedEnabled = false;
                std::cout << "Live feed: disabled" << std::endl;
            }
        }
    }

    void Engine::toggleAlerts(bool enable)
    {
        if(!m_alertManager)
            return;

        m_alertsEnabled = enable;
        m_alertManager->setEnabled(enable);
        std::cout << "Alerts: " << (enable ? "enabled" : "disabled") << std::endl;
    }

    bool Engine::exportImage(const std::string& path)
    {
        if(!m_recordingManager)
            return false;

        return m_recordingManager->captureScreenshot(path);
    }

    bool Engine::startVideoRecording(const std::string& path, int fps)
    {
        if(!m_recordingManager)
            return false;

        if(m_recordingManager->startRecording(path, fps))
        {
            m_recordingEnabled = true;
            return true;
        }

        return false;
    }

    bool Engine::stopVideoRecording()
    {
        if(!m_recordingManager)
            return false;

        if(m_recordingManager->stopRecording())
        {
            m_recordingEnabled = false;
            return true;
        }

        return false;
    }

    bool Engine::connectToFeed(const std::string& url, FeedType type)
    {
        if(!m_liveFeed)
        {
            m_liveFeed = std::make_unique<LiveFeed>(m_sim);
        }

        m_liveFeed->connect(url, type);
        m_liveFeedEnabled = true;
        return true;
    }

    bool Engine::disconnectFromFeed()
    {
        if(m_liveFeed)
        {
            m_liveFeed->disconnect();
            m_liveFeedEnabled = false;
            return true;
        }

        return false;
    }

    void Engine::setAlertCallback(AlertUICallback cb)
    {
        m_alertUICallback = cb;
    }

    void Engine::processAlert(AlertType type, uint32_t segmentId, const std::string& message)
    {
        if(m_alertUICallback)
        {
            m_alertUICallback(message, segmentId);
        }
    }

    void Engine::toggleKeybindingsWindow(bool enable)
    {
        m_showKeybindings = enable;
    }

    void Engine::toggleImGui(bool enable)
    {
        m_imguiEnabled = enable;
    }

    void Engine::toggleAntiAliasing(bool enable)
    {
        m_antiAliasingEnabled = enable;
        // Apply change to scene renderer
        m_scene->setAntiAliasing(enable);
    }
} // namespace tfv