#ifndef TFV_RENDERER_HPP
#define TFV_RENDERER_HPP

#include <cstdint>
#include <memory>
#include <string>

namespace tfv
{
    // Forward declarations for rendering backend types
    struct Window;
    struct RendererBackend;

    // Abstract rendering interface
    class IRenderer
    {
      public:
        virtual ~IRenderer() = default;

        // Initialization methods
        virtual bool initialize(void* windowHandle) = 0;
        virtual void shutdown() = 0;

        // Rendering methods
        virtual void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;
        virtual void present() = 0;
        virtual void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;
        virtual void drawLine(int x1, int y1, int x2, int y2) = 0;
        virtual void drawLine(int x1, int y1, int x2, int y2, int width) = 0;
        virtual void drawPoint(int x, int y) = 0;
        virtual void drawRect(int x, int y, int w, int h) = 0;
        virtual void fillRect(int x, int y, int w, int h) = 0;
        virtual void drawText(const std::string& text, int x, int y) = 0;

        // Control anti-aliasing for renderers that support it
        virtual void setAntiAliasing(bool enable) = 0;

        // Access to underlying renderer for backend-specific operations
        virtual void* getNativeRenderer() const = 0;

        // Get window size
        virtual void getWindowSize(int& width, int& height) const = 0;

        // Factory method to create a renderer
        static std::unique_ptr<IRenderer> create(const std::string& type = "SDL");
    };

    // SDL implementation
    class SDLRenderer : public IRenderer
    {
      public:
        SDLRenderer();
        ~SDLRenderer();

        bool initialize(void* windowHandle) override;
        void shutdown() override;

        void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void present() override;
        void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void drawLine(int x1, int y1, int x2, int y2) override;
        void drawLine(int x1, int y1, int x2, int y2, int width) override;
        void drawPoint(int x, int y) override;
        void drawRect(int x, int y, int w, int h) override;
        void fillRect(int x, int y, int w, int h) override;
        void drawText(const std::string& text, int x, int y) override;
        void setAntiAliasing(bool enable) override;

        void* getNativeRenderer() const override;
        void getWindowSize(int& width, int& height) const override;

      private:
        void* m_sdlWindow{nullptr};
        void* m_sdlRenderer{nullptr};
        bool m_antiAliasingEnabled{true};
        void* createTextTexture(const std::string& text);
    };

    // MetalKit implementation (stub)
    class MetalRenderer : public IRenderer
    {
      public:
        MetalRenderer();
        ~MetalRenderer();

        bool initialize(void* windowHandle) override;
        void shutdown() override;

        void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void present() override;
        void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void drawLine(int x1, int y1, int x2, int y2) override;
        void drawLine(int x1, int y1, int x2, int y2, int width) override;
        void drawPoint(int x, int y) override;
        void drawRect(int x, int y, int w, int h) override;
        void fillRect(int x, int y, int w, int h) override;
        void drawText(const std::string& text, int x, int y) override;
        void setAntiAliasing(bool enable) override;

        void* getNativeRenderer() const override;
        void getWindowSize(int& width, int& height) const override;

      private:
        void* m_metalView{nullptr};
        bool m_antiAliasingEnabled{true};
    };

} // namespace tfv

#endif
