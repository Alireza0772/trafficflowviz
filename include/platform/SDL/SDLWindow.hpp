// --- SDLWindow.hpp ---
#ifndef TFV_SDLWINDOW_HPP
#define TFV_SDLWINDOW_HPP

#include "core/Window.hpp" // Include the interface
#include <SDL2/SDL.h>

// Forward declare ImGui functions used here
struct ImGuiContext; // Not strictly needed but good practice
extern bool ImGui_ImplSDL2_InitForSDLRenderer(SDL_Window* window, SDL_Renderer* renderer);
extern void ImGui_ImplSDL2_Shutdown();
extern void ImGui_ImplSDL2_NewFrame();
extern bool ImGui_ImplSDL2_ProcessEvent(const SDL_Event* event);

namespace tfv
{

    class SDLWindow : public IWindow
    {
      public:
        SDLWindow();
        virtual ~SDLWindow();

        bool initialize(const WindowProps& props) override;
        void shutdown() override;

        void pollEvents() override;
        void swapBuffers() override; // SDL uses Renderer::present for this

        int getWidth() const override { return m_data.width; }
        int getHeight() const override { return m_data.height; }
        void setSize(int width, int height) override; // Implement using SDL_SetWindowSize

        void setEventCallback(const EventCallbackFn& callback) override
        {
            m_data.eventCallback = callback;
        }

        void* getNativeWindow() const override { return m_window; }

        // ImGui Platform Implementation
        void initImGuiPlatform() override;
        void shutdownImGuiPlatform() override;
        void newFrameImGuiPlatform() override;

      private:
        void initSDL();
        void shutdownSDL();
        void translateEvent(const SDL_Event& sdlEvent); // Translates SDL_Event to tfv::Event

        SDL_Window* m_window = nullptr;
        // Note: SDL_Renderer is part of the Renderer abstraction, not the window itself.
        // The window needs the renderer only temporarily during ImGui init.
        // Maybe pass it during initImGuiPlatform? Or get it from the Engine's Renderer?

        struct WindowData
        {
            std::string title;
            int width = 0, height = 0;
            EventCallbackFn eventCallback;
            // Add other state like vsync if needed
        };

        WindowData m_data;
        bool m_sdlInitialized = false;
    };

} // namespace tfv
#endif // TFV_SDLWINDOW_HPP
