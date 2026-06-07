// --- SDLWindow.cpp ---
#include "platform/SDL/SDLWindow.hpp"
#include "core/Events/KeyEvent.hpp"
#include "core/Events/MouseEvent.hpp"
#include "core/Events/WindowEvent.hpp"
#include "utils/LoggingManager.hpp" // Use your logger
#include <imgui.h>                  // Needed for ImGui context check maybe
#include <stdexcept>                // For exceptions

// Include ImGui SDL backend implementation details if needed

namespace tfv
{
    // Helper function to convert SDL mouse button to our enum
    MouseButton convertSDLMouseButton(Uint8 sdlButton)
    {
        switch(sdlButton)
        {
        case SDL_BUTTON_LEFT:   return MouseButton::Left;
        case SDL_BUTTON_RIGHT:  return MouseButton::Right;
        case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
        case SDL_BUTTON_X1:     return MouseButton::Button4;
        case SDL_BUTTON_X2:     return MouseButton::Button5;
        default:                return MouseButton::Left; // fallback
        }
    }
}
// Note: This is still platform-specific, but contained within this file.
#include <imgui_impl_sdl2.h> // We need the functions

namespace tfv
{

    // Factory implementation (usually in Window.cpp or a dedicated factory file)
    std::unique_ptr<Window> Window::create(const WindowProps& props)
    {
        // Later, you could switch here based on compile flags or config
        return std::make_unique<SDLWindow>();
    }

    SDLWindow::SDLWindow()
    { /* Default constructor */
    }

    SDLWindow::~SDLWindow()
    {
        shutdown(); // Ensure cleanup
    }

    void SDLWindow::initSDL()
    {
        if(!m_sdlInitialized)
        {
            if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
            {
                LOG_ERROR("SDL init failed: {error}", PARAM(error, SDL_GetError()));
                throw std::runtime_error("Failed to initialize SDL");
            }
            LOG_INFO("SDL Initialized");
            m_sdlInitialized = true; // Track SDL initialization globally if needed
        }
    }

    void SDLWindow::shutdownSDL()
    {
        // SDL_Quit should only be called once when the application truly exits,
        // not necessarily when a window is destroyed if you might create another.
        // This logic might belong in the Engine's destructor.
        // For now, assume one window lifecycle matches SDL lifecycle.
        if(m_sdlInitialized)
        {
            SDL_Quit();
            LOG_INFO("SDL Quit");
            m_sdlInitialized = false;
        }
    }

    bool SDLWindow::initialize(const WindowProps& props)
    {
        initSDL(); // Ensure SDL is up

        m_data.title = props.title;
        m_data.width = props.width;
        m_data.height = props.height;

        LOG_INFO("Creating window {title} ({w}x{h})", PARAM(title, m_data.title),
                 PARAM(w, m_data.width), PARAM(h, m_data.height));

        m_window = SDL_CreateWindow(m_data.title.c_str(), SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED, m_data.width, m_data.height,
                                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

        if(!m_window)
        {
            LOG_ERROR("Window creation failed: {error}", PARAM(error, SDL_GetError()));
            return false;
        }
        LOG_INFO("Window created successfully");

        // Icon loading remains similar, using SDL functions here
        SDL_Surface* icon = SDL_LoadBMP("assets/icon.bmp");
        if(icon)
        {
            SDL_SetWindowIcon(m_window, icon);
            SDL_FreeSurface(icon);
        }
        else
        {
            LOG_ERROR("Failed to load window icon: {error}", PARAM(error, SDL_GetError()));
        }

        return true;
    }

    void SDLWindow::shutdown()
    {
        if(m_window)
        {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
            LOG_INFO("Window destroyed");
        }
        // shutdownSDL(); // Move SDL_Quit to Engine destructor
    }

    void SDLWindow::pollEvents()
    {
        SDL_Event e;
        const bool hasImGui = (ImGui::GetCurrentContext() != nullptr);
        while(SDL_PollEvent(&e))
        {
            // Always let ImGui see every event first so it can track hover/focus
            // and decide whether it wants the mouse/keyboard. (The previous code
            // only forwarded events once WantCapture was already true, which never
            // happened without the prior events — so the UI was non-interactive.)
            if(hasImGui)
                ImGui_ImplSDL2_ProcessEvent(&e);

            // If ImGui is using the mouse/keyboard, consume the event so it does
            // not also drive the camera or engine hotkeys.
            if(hasImGui)
            {
                const ImGuiIO& io = ImGui::GetIO();
                const bool isMouse =
                    (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN ||
                     e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEWHEEL);
                const bool isKeyboard =
                    (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP || e.type == SDL_TEXTINPUT);
                if((isMouse && io.WantCaptureMouse) || (isKeyboard && io.WantCaptureKeyboard))
                    continue;
            }

            // Translate to the abstract event pipeline (engine + layers).
            translateEvent(e);
        }
    }

    void SDLWindow::translateEvent(const SDL_Event& sdlEvent)
    {
        if(!m_data.eventCallback)
            return; // No one listening

        // --- Translate SDL_Event to tfv::Event ---
        // Example translations:
        if(sdlEvent.type == SDL_QUIT)
        {
            WindowCloseEvent event;
            m_data.eventCallback(event);
        }
        else if(sdlEvent.type == SDL_WINDOWEVENT)
        {
            switch(sdlEvent.window.event)
            {
            case SDL_WINDOWEVENT_RESIZED:
            {
                m_data.width = sdlEvent.window.data1;
                m_data.height = sdlEvent.window.data2;
                WindowResizeEvent event(m_data.width, m_data.height);
                m_data.eventCallback(event);
                break;
            }
            case SDL_WINDOWEVENT_CLOSE:
            {
                WindowCloseEvent event;
                m_data.eventCallback(event);
                break;
            }
                // Add other window events (Focus, Moved etc.)
            }
        }
        else if(sdlEvent.type == SDL_KEYDOWN)
        {
            // Use SDL_GetScancodeFromKey(sdlEvent.key.keysym.sym) if you want scancodes
            KeyPressedEvent event(sdlEvent.key.keysym.sym, sdlEvent.key.repeat);
            m_data.eventCallback(event);
        }
        else if(sdlEvent.type == SDL_KEYUP)
        {
            KeyReleasedEvent event(sdlEvent.key.keysym.sym);
            m_data.eventCallback(event);
        }
        else if(sdlEvent.type == SDL_TEXTINPUT)
        {
            KeyTypedEvent event(sdlEvent.text.text); // Assuming KeyTypedEvent takes char* or string
            m_data.eventCallback(event);
        }
        else if(sdlEvent.type == SDL_MOUSEBUTTONDOWN)
        {
            MouseButtonPressedEvent event(convertSDLMouseButton(sdlEvent.button.button), 
                                         static_cast<float>(sdlEvent.button.x),
                                         static_cast<float>(sdlEvent.button.y));
            m_data.eventCallback(event);
        }
        else if(sdlEvent.type == SDL_MOUSEBUTTONUP)
        {
            MouseButtonReleasedEvent event(convertSDLMouseButton(sdlEvent.button.button), 
                                          static_cast<float>(sdlEvent.button.x),
                                          static_cast<float>(sdlEvent.button.y));
            m_data.eventCallback(event);
        }
        else if(sdlEvent.type == SDL_MOUSEWHEEL)
        {
            // Use the precise (fractional) delta on trackpads when available;
            // the integer wheel.x/y is quantized and jitters in sign, which made
            // zoom flicker between in and out.
#if SDL_VERSION_ATLEAST(2, 0, 18)
            float wx = sdlEvent.wheel.preciseX;
            float wy = sdlEvent.wheel.preciseY;
#else
            float wx = static_cast<float>(sdlEvent.wheel.x);
            float wy = static_cast<float>(sdlEvent.wheel.y);
#endif
            if(sdlEvent.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            {
                wx = -wx;
                wy = -wy;
            }
            // SDL wheel events carry no position; query it so the layer can
            // zoom toward the cursor.
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            MouseScrolledEvent event(wx, wy, static_cast<float>(mx), static_cast<float>(my));
            m_data.eventCallback(event);
        }
        else if(sdlEvent.type == SDL_MOUSEMOTION)
        {
            MouseMovedEvent event((float)sdlEvent.motion.x, (float)sdlEvent.motion.y);
            m_data.eventCallback(event);
        }
        // ... add more translations as needed
    }

    void SDLWindow::swapBuffers()
    {
        // This is typically handled by the Renderer's present() method with SDL_Renderer
        // If using OpenGL directly with SDL, you'd call SDL_GL_SwapWindow(m_window);
        // For now, this might do nothing if the Renderer handles presentation.
    }

    void SDLWindow::initImGuiPlatform()
    {
        // Problem: ImGui_ImplSDL2_InitForSDLRenderer needs the SDL_Renderer*.
        // How do we get it?
        // Option 1: The Engine passes it here.
        // Option 2: Query the Engine's Renderer for its native handle. (Preferred)
        // Let's assume we can get it (pseudo-code):
        // SDL_Renderer* sdlRenderer =
        // static_cast<SDL_Renderer*>(Engine::Get()->getRenderer()->getNativeRenderer()); if
        // (!sdlRenderer) { /* Handle error */ return; } bool success =
        // ImGui_ImplSDL2_InitForSDLRenderer(m_window, sdlRenderer); if (!success) { /* Handle error
        // */ }

        // Temporary workaround: Assume renderer is accessible somehow (Engine should manage this)
        // This highlights the tight coupling even with abstraction.
        // A better approach might involve the Engine coordinating this setup.
        // For now, let's assume it works via a placeholder:
        void* nativeRenderer =
            nullptr; // = Engine::GetRenderer()->getNativeRenderer(); // Placeholder
        if(nativeRenderer)
        {
            ImGui_ImplSDL2_InitForSDLRenderer(m_window, static_cast<SDL_Renderer*>(nativeRenderer));
            LOG_INFO("ImGui SDL2 Platform Backend Initialized");
        }
        else
        {
            LOG_WARN("Could not initialize ImGui SDL2 Platform Backend: Native Renderer not "
                     "available yet.");
            // This likely means initImGuiPlatform needs to be called *after* the renderer is
            // created.
        }
    }

    void SDLWindow::shutdownImGuiPlatform()
    {
        ImGui_ImplSDL2_Shutdown();
        LOG_INFO("ImGui SDL2 Platform Backend Shutdown");
    }

    void SDLWindow::newFrameImGuiPlatform()
    {
        ImGui_ImplSDL2_NewFrame();
    }

    void SDLWindow::setSize(int width, int height)
    {
        SDL_SetWindowSize(m_window, width, height);
        // Note: This might trigger a resize event, which should update m_data.width/height
    }

} // namespace tfv