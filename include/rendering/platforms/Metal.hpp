#pragma once

#include "rendering/Renderer.hpp"

namespace tfv
{
    // Native Metal rendering backend (macOS). Renders everything as batched per-vertex-colored
    // triangles through a single MSAA pipeline; the base-class vector primitives + all the draw
    // calls funnel into fillGeometry, which appends to a per-frame vertex batch flushed at
    // present(). Obj-C/Metal state lives behind the opaque m_res pointer.
    class MetalRenderer : public Renderer
    {
      public:
        explicit MetalRenderer(void* window); // window = SDL_Window*
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
        void fillGeometry(const RVertex* verts, int vcount, const int* idx, int icount) override;
        void setAntiAliasing(bool enable) override;
        void* getNativeRenderer() const override;
        void getWindowSize(int& width, int& height) const override;
        void getDrawableSize(int& width, int& height) const override;

      private:
        void* m_window{nullptr};        // SDL_Window*
        void* m_res{nullptr};           // MetalResources* (Obj-C state)
        bool m_antiAliasingEnabled{true};
    };
} // namespace tfv
