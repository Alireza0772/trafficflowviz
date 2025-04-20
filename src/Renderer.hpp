#ifndef TFV_RENDERER_HPP
#define TFV_RENDERER_HPP

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
        virtual void setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;
        virtual void drawLine(int x1, int y1, int x2, int y2) = 0;
        virtual void drawPoint(int x, int y) = 0;
    };

    // SDL implementation
    class SDLRenderer : public IRenderer
    {
      public:
        SDLRenderer(void* sdlRenderer); // expects SDL_Renderer*
        ~SDLRenderer();
        void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void present() override;
        void setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void drawLine(int x1, int y1, int x2, int y2) override;
        void drawPoint(int x, int y) override;

      private:
        void* m_sdlRenderer; // SDL_Renderer*
    };

    // MetalKit implementation (stub)
    class MetalRenderer : public IRenderer
    {
      public:
        MetalRenderer(void* metalView); // expects a pointer to a MetalKit view
        ~MetalRenderer();
        void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void present() override;
        void setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void drawLine(int x1, int y1, int x2, int y2) override;
        void drawPoint(int x, int y) override;

      private:
        void* m_metalView;
    };

} // namespace tfv

#endif
