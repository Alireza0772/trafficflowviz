#ifndef TFV_IMGUI_RENDERER_HPP
#define TFV_IMGUI_RENDERER_HPP

#include <SDL2/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <functional>
#include <string>

namespace tfv
{
    class ImGuiRenderer
    {
      public:
        ImGuiRenderer(SDL_Window* window, SDL_Renderer* renderer);
        ~ImGuiRenderer();

        void init();
        void beginFrame();
        void endFrame();
        void processEvent(const SDL_Event& event);

        // Show keyboard shortcuts
        void showKeybindingsWindow(bool* p_open = nullptr);

      private:
        SDL_Window* m_window;
        SDL_Renderer* m_renderer;
        bool m_initialized;
    };
} // namespace tfv

#endif // TFV_IMGUI_RENDERER_HPP