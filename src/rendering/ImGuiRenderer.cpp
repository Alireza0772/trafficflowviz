#include "rendering/ImGuiRenderer.hpp"

#include <iostream>

namespace tfv
{
    ImGuiRenderer::ImGuiRenderer(SDL_Window* window, SDL_Renderer* renderer)
        : m_window(window), m_renderer(renderer), m_initialized(false)
    {
    }

    ImGuiRenderer::~ImGuiRenderer()
    {
        if(m_initialized)
        {
            ImGui_ImplSDLRenderer2_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
        }
    }

    void ImGuiRenderer::init()
    {
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

        // Enable docking and multi-viewport support
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport

        // Configure docking
        io.ConfigDockingWithShift = false;      // Enable docking without shift
        io.ConfigWindowsResizeFromEdges = true; // Enable resizing from edges

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look
        // identical to regular ones
        ImGuiStyle& style = ImGui::GetStyle();
        if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        // Setup Platform/Renderer backends
        ImGui_ImplSDL2_InitForSDLRenderer(m_window, m_renderer);
        ImGui_ImplSDLRenderer2_Init(m_renderer);

        m_initialized = true;
    }

    void ImGuiRenderer::beginFrame()
    {
        if(!m_initialized)
            init();

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiRenderer::endFrame()
    {
        ImGui::Render();
        // Pass the renderer explicitly as required by the function signature
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_renderer);

        // Update and Render additional Platform Windows
        ImGuiIO& io = ImGui::GetIO();
        if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
            SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
        }
    }

    void ImGuiRenderer::processEvent(const SDL_Event& event)
    {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }

    void ImGuiRenderer::showKeybindingsWindow(bool* p_open)
    {
        if(ImGui::Begin("Keybindings", p_open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Navigation Controls:");
            ImGui::Separator();
            ImGui::Columns(2, "keybindings");
            ImGui::SetColumnWidth(0, 120);

            ImGui::Text("Arrow Keys");
            ImGui::NextColumn();
            ImGui::Text("Pan the view");
            ImGui::NextColumn();

            ImGui::Text("+/=");
            ImGui::NextColumn();
            ImGui::Text("Zoom in");
            ImGui::NextColumn();

            ImGui::Text("-");
            ImGui::NextColumn();
            ImGui::Text("Zoom out");
            ImGui::NextColumn();

            ImGui::Text("Mouse Wheel");
            ImGui::NextColumn();
            ImGui::Text("Zoom in/out");
            ImGui::NextColumn();

            ImGui::Columns(1);
            ImGui::Separator();
            ImGui::Text("Feature Toggles:");
            ImGui::Separator();
            ImGui::Columns(2);

            ImGui::Text("H");
            ImGui::NextColumn();
            ImGui::Text("Toggle heatmap");
            ImGui::NextColumn();

            ImGui::Text("L");
            ImGui::NextColumn();
            ImGui::Text("Toggle live feed");
            ImGui::NextColumn();

            ImGui::Text("A");
            ImGui::NextColumn();
            ImGui::Text("Toggle alerts");
            ImGui::NextColumn();

            ImGui::Text("R");
            ImGui::NextColumn();
            ImGui::Text("Toggle recording");
            ImGui::NextColumn();

            ImGui::Text("I");
            ImGui::NextColumn();
            ImGui::Text("Toggle ImGui");
            ImGui::NextColumn();

            ImGui::Text("G");
            ImGui::NextColumn();
            ImGui::Text("Toggle anti-aliasing");
            ImGui::NextColumn();

            ImGui::Columns(1);
            ImGui::Separator();
            ImGui::Text("Export Functions:");
            ImGui::Separator();
            ImGui::Columns(2);

            ImGui::Text("S");
            ImGui::NextColumn();
            ImGui::Text("Save screenshot");
            ImGui::NextColumn();

            ImGui::Columns(1);
            ImGui::Separator();
            ImGui::Text("System Controls:");
            ImGui::Separator();
            ImGui::Columns(2);

            ImGui::Text("ESC");
            ImGui::NextColumn();
            ImGui::Text("Exit application");
            ImGui::NextColumn();

            ImGui::Columns(1);
        }
        ImGui::End();
    }
} // namespace tfv