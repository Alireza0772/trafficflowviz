#ifndef TFV_RENDERER_HPP
#define TFV_RENDERER_HPP

#include <SDL2/SDL.h>
#include <cstdint>
#include <string>

namespace tfv
{

    // Abstract rendering interface
    class IRenderer
    {
      public:
        virtual ~IRenderer() = default;
        virtual void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;
        virtual void present() = 0;
        virtual void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;
        virtual void drawLine(int x1, int y1, int x2, int y2) = 0;
        virtual void drawLine(int x1, int y1, int x2, int y2, int width) = 0;
        virtual void drawPoint(int x, int y) = 0;
        virtual void drawRect(int x, int y, int w, int h) = 0;
        virtual void fillRect(int x, int y, int w, int h) = 0;
        virtual void drawText(const std::string& text, int x, int y) = 0;
    };

    // SDL implementation
    class SDLRenderer : public IRenderer
    {
      public:
        SDLRenderer(SDL_Renderer* sdlRenderer);
        ~SDLRenderer();
        void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void present() override;
        void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void drawLine(int x1, int y1, int x2, int y2) override;
        void drawLine(int x1, int y1, int x2, int y2, int width) override;
        void drawPoint(int x, int y) override;
        void drawRect(int x, int y, int w, int h) override;
        void fillRect(int x, int y, int w, int h) override;
        void drawText(const std::string& text, int x, int y) override;

        // Access the underlying SDL renderer
        SDL_Renderer* getSDLRenderer() const { return m_sdlRenderer; }

      private:
        SDL_Renderer* m_sdlRenderer;
        SDL_Texture* createTextTexture(const std::string& text);
    };

    // MetalKit implementation (stub)
    class MetalRenderer : public IRenderer
    {
      public:
        MetalRenderer(void* metalView); // expects a pointer to a MetalKit view
        ~MetalRenderer();
        void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void present() override;
        void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void drawLine(int x1, int y1, int x2, int y2) override;
        void drawLine(int x1, int y1, int x2, int y2, int width) override;
        void drawPoint(int x, int y) override;
        void drawRect(int x, int y, int w, int h) override;
        void fillRect(int x, int y, int w, int h) override;
        void drawText(const std::string& text, int x, int y) override;

      private:
        void* m_metalView;
    };

} // namespace tfv

#endif
