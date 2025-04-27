#pragma once

#include "rendering/Renderer.hpp"

namespace tfv
{
    class MetalRenderer : public Renderer
    {
      public:
        MetalRenderer();
        ~MetalRenderer();

        // Implement Renderer interface
        bool initialize() override;
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
        void* windowHandle;
        void* m_metalView;
        void* m_metalResources;
        bool m_antiAliasingEnabled;
    };
} // namespace tfv