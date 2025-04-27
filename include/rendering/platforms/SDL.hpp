#pragma once
#include "rendering/Renderer.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
namespace tfv
{
    class SDLRenderer : public Renderer
    {
      public:
        SDLRenderer(SDL_Window* window);
        ~SDLRenderer();

        // Initialization methods
        bool initialize() override;
        void shutdown() override;

        // Rendering methods
        void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void present() override;
        void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void drawLine(int x1, int y1, int x2, int y2) override;
        void drawLine(int x1, int y1, int x2, int y2, int width) override;
        void drawPoint(int x, int y) override;
        void drawRect(int x, int y, int w, int h) override;
        void fillRect(int x, int y, int w, int h) override;
        void drawText(const std::string& text, int x, int y) override;

        // Control anti-aliasing for renderers that support it
        void setAntiAliasing(bool enable) override;

        // Access to underlying renderer for backend-specific operations
        void* getNativeRenderer() const override;

        // Get window size
        void getWindowSize(int& width, int& height) const override;

      private:
        SDL_Texture* createTextTexture(const std::string& text);

      private:
        SDL_Renderer* m_renderer;
        SDL_Window* m_window;
        bool m_antiAliasingEnabled;
    };
} // namespace tfv