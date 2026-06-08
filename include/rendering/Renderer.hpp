#ifndef TFV_RENDERER_HPP
#define TFV_RENDERER_HPP

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace tfv
{
    // Per-vertex-colored vertex for fillGeometry (position in framebuffer px, RGBA).
    struct RVertex
    {
        float x, y;
        uint8_t r, g, b, a;
    };

    // Abstract rendering interface, templated on the backend type and window type
    class Renderer
    {
      public:
        // Factory method to create a renderer instance with a specific type
        static std::unique_ptr<Renderer> create(const std::string& type, void* window);

        // Constructor and destructor
        Renderer() = default;                          // Default constructor
        Renderer(const Renderer&) = delete;            // Disable copy constructor
        Renderer& operator=(const Renderer&) = delete; // Disable copy assignment
        Renderer(Renderer&&) = default;                // Enable move constructor
        Renderer& operator=(Renderer&&) = default;     // Enable move assignment
        // Virtual destructor for proper cleanup of derived classes
        virtual ~Renderer() = default;

        // Initialization methods
        virtual bool initialize() = 0;
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

        // Filled, per-vertex-colored triangle soup (idx into verts). Additive: the
        // default is a no-op, so backends without geometry support still compile/link.
        virtual void fillGeometry(const RVertex* verts, int vcount, const int* idx, int icount)
        {
            (void)verts;
            (void)vcount;
            (void)idx;
            (void)icount;
        }

        // Convenience: a filled convex quad a->b->c->d (two triangles, per-vertex color).
        void fillQuad(const RVertex& a, const RVertex& b, const RVertex& c, const RVertex& d)
        {
            const RVertex v[4] = {a, b, c, d};
            const int idx[6] = {0, 1, 2, 0, 2, 3};
            fillGeometry(v, 4, idx, 6);
        }

        // --- Vector primitives. Default impls tessellate via fillGeometry, so every backend
        //     gets them; a backend (e.g. a future NanoVG) may override for native acceleration.
        //     Colors are explicit so each fill/stroke is self-contained (no setColor state). ---
        virtual void fillCircle(float cx, float cy, float radius, uint8_t r, uint8_t g, uint8_t b,
                                uint8_t a, int segments = 28);
        virtual void strokeCircle(float cx, float cy, float radius, float width, uint8_t r,
                                  uint8_t g, uint8_t b, uint8_t a, int segments = 28);
        // Thick stroked polyline with round joins; closed connects last->first.
        virtual void strokePolyline(const glm::vec2* pts, int n, float width, uint8_t r, uint8_t g,
                                    uint8_t b, uint8_t a, bool roundJoin = true, bool closed = false);
        // Filled (convex) polygon, triangle-fan from pts[0].
        virtual void fillPolygon(const glm::vec2* pts, int n, uint8_t r, uint8_t g, uint8_t b,
                                 uint8_t a);
        // Flatten a quadratic/cubic Bezier into `out` (appends steps+1 points).
        static void flattenQuadratic(glm::vec2 p0, glm::vec2 c, glm::vec2 p1, int steps,
                                     std::vector<glm::vec2>& out);
        static void flattenCubic(glm::vec2 p0, glm::vec2 c0, glm::vec2 c1, glm::vec2 p1, int steps,
                                 std::vector<glm::vec2>& out);

        // Control anti-aliasing for renderers that support it
        virtual void setAntiAliasing(bool enable) = 0;

        // Access to underlying renderer for backend-specific operations
        virtual void* getNativeRenderer() const = 0;

        // Get window size
        virtual void getWindowSize(int& width, int& height) const = 0;
    };
} // namespace tfv
#endif
