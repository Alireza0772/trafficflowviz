#include "rendering/layers/ImGuiLayer.hpp"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <iostream>

namespace tfv
{

    ImGuiLayer::ImGuiLayer(SDL_Window* window, SDL_Renderer* renderer, Simulation* simulation)
        : m_window(window), m_renderer(renderer), m_simulation(simulation)
    {
        setName("ImGuiLayer");
        setZIndex(100); // Highest z-index to render on top
    }

    ImGuiLayer::~ImGuiLayer()
    {
        onDetach();
    }

    void ImGuiLayer::onAttach()
    {
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();

        // Setup Platform/Renderer backends
        ImGui_ImplSDL2_InitForSDLRenderer(m_window, m_renderer);
        ImGui_ImplSDLRenderer2_Init(m_renderer);

        m_initialized = true;
        m_showKeybindings = true; // Show keybindings by default
    }

    void ImGuiLayer::onDetach()
    {
        if(m_initialized)
        {
            ImGui_ImplSDLRenderer2_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
            m_initialized = false;
        }
    }

    bool ImGuiLayer::onEvent(void* event)
    {
        if(!m_initialized || !event)
            return false;

        SDL_Event* sdlEvent = static_cast<SDL_Event*>(event);
        ImGui_ImplSDL2_ProcessEvent(sdlEvent);

        // Check if ImGui wants to capture the event
        ImGuiIO& io = ImGui::GetIO();
        bool captured = io.WantCaptureMouse || io.WantCaptureKeyboard;

        return captured;
    }

    void ImGuiLayer::onUpdate(double dt)
    {
        // Nothing to update
    }

    void ImGuiLayer::onRender()
    {
        if(!m_initialized)
            return;

        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiLayer::onImGuiRender()
    {
        if(!m_initialized)
            return;

        // Create dockspace
        renderDockspace();

        // Render menu bar
        renderMainMenuBar();

        // Render keybindings if enabled
        if(m_showKeybindings)
        {
            renderKeybindingsWindow();
        }

        // Render status bar at the bottom
        renderStatusBar();

        // Render ImGui
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_renderer);
    }

    void ImGuiLayer::renderDockspace()
    {
        static bool dockspaceOpen = true;
        static bool opt_fullscreen = true;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;

        // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not
        // dockable into, because it would be confusing to have two docking targets within each
        // others.
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if(opt_fullscreen)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }

        // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
        // and handle the pass-thru hole, so we ask Begin() to not render a background.
        if(dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            window_flags |= ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpace", &dockspaceOpen, window_flags);
        ImGui::PopStyleVar();

        if(opt_fullscreen)
            ImGui::PopStyleVar(2);

        // Submit the DockSpace
        ImGuiIO& io = ImGui::GetIO();
        if(io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        ImGui::End();
    }

    void ImGuiLayer::renderMainMenuBar()
    {
        if(ImGui::BeginMainMenuBar())
        {
            if(ImGui::BeginMenu("File"))
            {
                if(ImGui::MenuItem("Exit", "Esc"))
                {
                    // Request application shutdown
                    SDL_Event quitEvent;
                    quitEvent.type = SDL_QUIT;
                    SDL_PushEvent(&quitEvent);
                }
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("View"))
            {
                // Toggle keybindings window
                bool keybindings = m_showKeybindings;
                if(ImGui::MenuItem("Keybindings", "K", &keybindings))
                {
                    m_showKeybindings = keybindings;
                }

                // Toggle layers
                if(m_simulationLayer)
                {
                    bool simEnabled = m_simulationLayer->isEnabled();
                    if(ImGui::MenuItem("Simulation Layer", nullptr, &simEnabled))
                    {
                        m_simulationLayer->setEnabled(simEnabled);
                    }
                }

                // Get heatmap layer and toggle it
                if(ImGui::MenuItem("Heatmap", "H"))
                {
                    auto* layer = dynamic_cast<Layer*>(m_simulationLayer);
                    if(layer)
                    {
                        // Find heatmap layer in same layer stack
                        // Note: In a full implementation, the Engine would maintain
                        // references to all layers
                    }
                }

                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Recording"))
            {
                if(m_recordingManager)
                {
                    if(ImGui::MenuItem(m_recordingManager->isRecording() ? "Stop Recording"
                                                                         : "Start Recording",
                                       "R"))
                    {
                        // Toggle recording
                        if(m_recordingManager->isRecording())
                        {
                            m_recordingManager->stopRecording();
                        }
                        else
                        {
                            m_recordingManager->startRecording("output.mp4", 30);
                        }
                    }

                    if(ImGui::MenuItem("Take Screenshot", "S"))
                    {
                        m_recordingManager->captureScreenshot("screenshot.png");
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
    }

    void ImGuiLayer::renderStatusBar()
    {
        // Create status bar at bottom of screen
        const float height = 20.0f;
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoDocking;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 work_pos = viewport->WorkPos;
        ImVec2 work_size = viewport->WorkSize;
        ImVec2 window_pos, window_size;

        window_pos.x = work_pos.x;
        window_pos.y = work_pos.y + work_size.y - height;
        window_size.x = work_size.x;
        window_size.y = height;

        ImGui::SetNextWindowPos(window_pos);
        ImGui::SetNextWindowSize(window_size);

        if(ImGui::Begin("StatusBar", nullptr, window_flags))
        {
            ImGui::Text("FPS: %d", m_fps);

            ImGui::SameLine(100);
            int vehicleCount = m_simulation ? m_simulation->snapshot().size() : 0;
            ImGui::Text("Vehicles: %d", vehicleCount);

            if(m_simulationLayer)
            {
                ImGui::SameLine(200);
                ImGui::Text("Zoom: %.1fx", m_simulationLayer->getZoom());
            }

            if(m_recordingManager && m_recordingManager->isRecording())
            {
                ImGui::SameLine(300);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
                ImGui::Text("● RECORDING");
                ImGui::PopStyleColor();
            }
        }
        ImGui::End();
    }

    void ImGuiLayer::renderKeybindingsWindow()
    {
        ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
        if(ImGui::Begin("Keybindings", &m_showKeybindings))
        {
            ImGui::Text("Navigation Controls:");
            ImGui::BulletText("Arrow Keys - Pan the view");
            ImGui::BulletText("Mouse Wheel - Zoom in/out");
            ImGui::BulletText("+/- Keys - Zoom in/out");

            ImGui::Separator();

            ImGui::Text("Feature Toggles:");
            ImGui::BulletText("H - Toggle heatmap");
            ImGui::BulletText("L - Toggle live feed");
            ImGui::BulletText("A - Toggle alerts");
            ImGui::BulletText("R - Toggle recording");
            ImGui::BulletText("I - Toggle ImGui interface");
            ImGui::BulletText("G - Toggle anti-aliasing");
            ImGui::BulletText("K - Toggle this window");

            ImGui::Separator();

            ImGui::Text("Other Controls:");
            ImGui::BulletText("S - Save screenshot");
            ImGui::BulletText("Esc - Exit application");
        }
        ImGui::End();
    }

    void ImGuiLayer::HelpMarker(const char* desc)
    {
        ImGui::TextDisabled("(?)");
        if(ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

} // namespace tfv