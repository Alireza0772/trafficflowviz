#include "rendering/layers/ImGuiLayer.hpp"
#include "core/Configuration.hpp"
#include "core/Simulation.hpp"
#include "rendering/layers/SimulationLayer.hpp"
#include "utils/LoggingManager.hpp"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <iostream>
#include <string>

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

        // Hi-DPI crisp fonts: rasterize the atlas at the framebuffer scale and lay
        // it out at 1/scale. Since onImGuiRender render-scales ImGui by the same
        // framebuffer scale, text ends up sharp instead of an upscaled bitmap.
        {
            int ww = 0, wh = 0, rw = 0, rh = 0;
            SDL_GetWindowSize(m_window, &ww, &wh);
            SDL_GetRendererOutputSize(m_renderer, &rw, &rh);
            float dpiScale = (ww > 0) ? static_cast<float>(rw) / static_cast<float>(ww) : 1.0f;
            if(dpiScale < 1.0f)
                dpiScale = 1.0f;

            const float baseFontPx = 16.0f;
            ImFont* font = io.Fonts->AddFontFromFileTTF(
                "/System/Library/Fonts/Supplemental/Arial.ttf", baseFontPx * dpiScale);
            if(!font)
                io.Fonts->AddFontDefault(); // fall back to the embedded font
            io.FontGlobalScale = 1.0f / dpiScale;
        }

        // Setup Platform/Renderer backends
        ImGui_ImplSDL2_InitForSDLRenderer(m_window, m_renderer);
        ImGui_ImplSDLRenderer2_Init(m_renderer);

        m_initialized = true;
        // Keybindings visibility is controlled by the Engine (showKeybindingsWindow,
        // toggled with 'K'); do not force it on here so the scene is unobstructed
        // on first run.
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

    bool ImGuiLayer::onEvent(Event& event)
    {
        // Raw SDL events are fed to ImGui (and consumed when ImGui wants the
        // mouse/keyboard) at the window event-pump level (SDLWindow::pollEvents),
        // so there is nothing to do per-layer here.
        (void)event;
        return false;
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

        if(m_showInspector)
        {
            renderVehicleInspector();
        }

        if(m_showLegend)
        {
            renderLegend();
        }

        // Render status bar at the bottom
        renderStatusBar();

        // Render ImGui. On hi-DPI (Retina) displays the SDL renderer output is the
        // full pixel framebuffer while ImGui lays out in logical points, so scale
        // the renderer by the framebuffer scale before submitting ImGui draw data
        // (otherwise the whole UI is drawn into the top-left quarter at half size).
        // Reset to 1.0 afterwards so the scene keeps rendering at native pixels.
        ImGui::Render();
        ImGuiIO& io = ImGui::GetIO();
        SDL_RenderSetScale(m_renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_renderer);
        SDL_RenderSetScale(m_renderer, 1.0f, 1.0f);
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
                // Start/stop trajectory + metrics export to files.
                if(m_exportToggleCallback)
                {
                    bool exporting = m_exportEnabled;
                    if(ImGui::MenuItem(exporting ? "Stop Export" : "Export Trajectory", nullptr,
                                       &exporting)) // menu-only; no key handler bound
                    {
                        m_exportEnabled = exporting;
                        m_exportToggleCallback(m_exportEnabled);
                    }
                }
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

                // Toggle the vehicle inspector (menu-only; 'I' is the global ImGui toggle)
                bool inspector = m_showInspector;
                if(ImGui::MenuItem("Vehicle Inspector", nullptr, &inspector))
                {
                    m_showInspector = inspector;
                }

                // Toggle the perception debug overlay
                if(m_debugPerceptionLayer)
                {
                    bool overlay = m_debugPerceptionLayer->isEnabled();
                    if(ImGui::MenuItem("Perception Overlay", nullptr, &overlay)) // menu-only
                        m_debugPerceptionLayer->setEnabled(overlay);
                }

                // Vehicle color encoding + legend
                if(m_simulationLayer)
                {
                    const ColorEncoding cur = m_simulationLayer->colorEncoding();
                    if(ImGui::BeginMenu("Color"))
                    {
                        if(ImGui::MenuItem("Speed", nullptr, cur == ColorEncoding::Speed))
                            m_simulationLayer->setColorEncoding(ColorEncoding::Speed);
                        if(ImGui::MenuItem("Lane", nullptr, cur == ColorEncoding::Lane))
                            m_simulationLayer->setColorEncoding(ColorEncoding::Lane);
                        if(ImGui::MenuItem("Vehicle Class", nullptr,
                                           cur == ColorEncoding::VehicleClass))
                            m_simulationLayer->setColorEncoding(ColorEncoding::VehicleClass);
                        ImGui::EndMenu();
                    }
                }
                bool legend = m_showLegend;
                if(ImGui::MenuItem("Legend", nullptr, &legend))
                    m_showLegend = legend;

                // Scene theme (also swaps the ImGui style to match).
                if(m_simulationLayer && ImGui::BeginMenu("Theme"))
                {
                    const SceneTheme th = m_simulationLayer->sceneTheme();
                    if(ImGui::MenuItem("Studio Dark", nullptr, th == SceneTheme::StudioDark))
                    {
                        m_simulationLayer->setSceneTheme(SceneTheme::StudioDark);
                        ImGui::StyleColorsDark();
                    }
                    if(ImGui::MenuItem("Paper Light", nullptr, th == SceneTheme::PaperLight))
                    {
                        m_simulationLayer->setSceneTheme(SceneTheme::PaperLight);
                        ImGui::StyleColorsLight();
                    }
                    ImGui::EndMenu();
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
            int vehicleCount = m_simulation ? static_cast<int>(m_simulation->vehicleCount()) : 0;
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

        renderWorldGenWindow();
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

    void ImGuiLayer::renderWorldGenWindow()
    {
        auto& cfg = TFV_CONFIG();
        // Editable copies, seeded once from the live config so the panel reflects current values.
        static bool init = false;
        static int seed = 12345, rows = 7, cols = 11, hubs = 3, districts = 3, vehicles = 60;
        static int modeIdx = 0;
        static float arterial = 320.0f, street = 160.0f;
        if(!init)
        {
            seed = static_cast<int>(cfg.getMasterSeed());
            rows = cfg.getInt("sim.proc.rows", 7);
            cols = cfg.getInt("sim.proc.cols", 11);
            hubs = cfg.getInt("sim.proc.hubs", 3);
            districts = cfg.getInt("sim.proc.districts", 3);
            vehicles = cfg.getInt("sim.proc.vehicles", 60);
            arterial = cfg.getFloat("sim.proc.arterial_spacing_m", 320.0f);
            street = cfg.getFloat("sim.proc.street_spacing_m", 160.0f);
            modeIdx = (cfg.getString("sim.proc.mode", "city") == "grid") ? 1 : 0;
            init = true;
        }

        ImGui::SetNextWindowSize(ImVec2(330, 360), ImGuiCond_FirstUseEver);
        if(ImGui::Begin("World Generation"))
        {
            if(m_simulationLayer)
            {
                bool graph = m_simulationLayer->graphView();
                if(ImGui::Checkbox("Graph view", &graph))
                    m_simulationLayer->setGraphView(graph);
                ImGui::Separator();
            }

            const char* modes[] = {"city (tensor field)", "grid"};
            ImGui::Combo("Mode", &modeIdx, modes, 2);
            ImGui::InputInt("Seed", &seed);
            ImGui::SliderInt("Hubs", &hubs, 0, 8);
            ImGui::SliderInt("Districts", &districts, 1, 6);
            ImGui::SliderInt("Rows", &rows, 3, 16);
            ImGui::SliderInt("Cols", &cols, 3, 22);
            ImGui::SliderFloat("Arterial gap", &arterial, 140.0f, 600.0f, "%.0f m");
            ImGui::SliderFloat("Street gap", &street, 80.0f, 320.0f, "%.0f m");
            ImGui::SliderInt("Vehicles", &vehicles, 0, 400);
            ImGui::Separator();

            auto apply = [&]() {
                cfg.setValue("sim.master_seed", std::to_string(seed));
                cfg.setValue("sim.proc.mode", modeIdx == 1 ? "grid" : "city");
                cfg.setValue("sim.proc.rows", std::to_string(rows));
                cfg.setValue("sim.proc.cols", std::to_string(cols));
                cfg.setValue("sim.proc.hubs", std::to_string(hubs));
                cfg.setValue("sim.proc.districts", std::to_string(districts));
                cfg.setValue("sim.proc.vehicles", std::to_string(vehicles));
                cfg.setValue("sim.proc.arterial_spacing_m", std::to_string(static_cast<int>(arterial)));
                cfg.setValue("sim.proc.street_spacing_m", std::to_string(static_cast<int>(street)));
                if(m_simulation)
                    m_simulation->regenerate();
                if(m_simulationLayer)
                    m_simulationLayer->refreshNetwork(); // re-point renderer + re-frame camera
            };

            if(ImGui::Button("Regenerate"))
                apply();
            ImGui::SameLine();
            if(ImGui::Button("New seed"))
            {
                seed = static_cast<int>((static_cast<unsigned>(seed) * 1103515245u + 12345u) &
                                        0x7fffffffu);
                apply();
            }
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

    void ImGuiLayer::renderVehicleInspector()
    {
        if(ImGui::Begin("Vehicle Inspector", &m_showInspector))
        {
            const uint64_t id = m_simulationLayer ? m_simulationLayer->selectedVehicleId() : 0;
            if(id == 0)
            {
                ImGui::TextWrapped(
                    "Click a vehicle in the scene to inspect its perception and decision.");
            }
            else
            {
                const VehicleInspection ins = m_simulation->inspect(id);
                if(!ins.found)
                {
                    ImGui::Text("Vehicle #%llu not found (despawned).", (unsigned long long)id);
                }
                else
                {
                    ImGui::Text("Vehicle #%llu   brain: %s", (unsigned long long)id,
                                m_simulation->brainKind().c_str());
                    ImGui::Text("segment %u  lane %u  speed %.2f m/s  leader %lld  violations %u",
                                ins.segmentId, (unsigned)ins.laneIndex, ins.speed,
                                (long long)ins.leaderId, ins.violations);
                    ImGui::Separator();
                    const Action& a = ins.action;
                    ImGui::Text("Action:  accel %.3f   laneChange %d   turn %d   lights 0x%02X",
                                a.accel, (int)a.laneChange, (int)a.turn, (unsigned)a.lightCmd);
                    ImGui::Separator();
                    static const char* kLabels[24] = {
                        "self_speed",   "self_accel",    "lane_fraction", "position",
                        "speed_limit",  "congestion",    "dist_intersxn", "lane_count",
                        "signal_phase", "signal_ttc",    "sign_type",     "front_gap",
                        "front_relspd", "front_leader",  "rear_dist",     "rear_relspd",
                        "rear_light",   "left_dist",     "left_relspd",   "left_light",
                        "right_dist",   "right_relspd",  "right_light",   "reserved"};
                    if(ImGui::CollapsingHeader("Observation (24 channels)",
                                               ImGuiTreeNodeFlags_DefaultOpen))
                        for(int i = 0; i < 24; ++i)
                            ImGui::Text("  [%2d] %-14s % .4f", i, kLabels[i], ins.obs[i]);
                    if(ImGui::CollapsingHeader("Perception sectors", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        static const char* sn[4] = {"Front", "Rear", "Left", "Right"};
                        for(int s = 0; s < 4; ++s)
                        {
                            const SensedNeighbor& n = ins.sectors[s];
                            if(n.valid)
                                ImGui::Text("  %-5s id %llu  dist %.2f  relSpd %.2f  lights 0x%02X",
                                            sn[s], (unsigned long long)n.id, n.relDist, n.relSpeed,
                                            (unsigned)n.lightBits);
                            else
                                ImGui::Text("  %-5s (clear)", sn[s]);
                        }
                    }
                }
            }
        }
        ImGui::End();
    }

    void ImGuiLayer::renderLegend()
    {
        if(ImGui::Begin("Legend", &m_showLegend))
        {
            const ColorEncoding e =
                m_simulationLayer ? m_simulationLayer->colorEncoding() : ColorEncoding::Speed;
            ImGui::Text("Vehicle color: %s", encodingName(e));
            ImGui::Separator();
            if(e == ColorEncoding::Speed)
            {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float w = 180.0f, h = 14.0f;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImU32 g = IM_COL32(46, 204, 113, 255);
                const ImU32 a = IM_COL32(241, 196, 15, 255);
                const ImU32 r = IM_COL32(231, 76, 60, 255);
                dl->AddRectFilledMultiColor(p, ImVec2(p.x + w * 0.5f, p.y + h), g, a, a, g);
                dl->AddRectFilledMultiColor(ImVec2(p.x + w * 0.5f, p.y), ImVec2(p.x + w, p.y + h), a,
                                            r, r, a);
                ImGui::Dummy(ImVec2(w, h));
                ImGui::Text("0");
                ImGui::SameLine(w - 56.0f);
                ImGui::Text("13.9 m/s");
            }
            else
            {
                auto swatch = [&](RGB8 c, const char* label) {
                    ImGui::ColorButton(label,
                                       ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f),
                                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                                       ImVec2(16, 16));
                    ImGui::SameLine();
                    ImGui::TextUnformatted(label);
                };
                if(e == ColorEncoding::Lane)
                {
                    const char* names[] = {"Lane 0", "Lane 1", "Lane 2", "Lane 3"};
                    for(int i = 0; i < 4; ++i)
                        swatch(laneRGB(static_cast<uint8_t>(i)), names[i]);
                }
                else
                {
                    const char* types[] = {"car", "truck", "bus", "emergency"};
                    for(const char* t : types)
                        swatch(classRGB(t), t);
                }
            }
            ImGui::Separator();
            if(m_simulation)
                ImGui::Text("Brain: %s", m_simulation->brainKind().c_str());
        }
        ImGui::End();
    }

} // namespace tfv