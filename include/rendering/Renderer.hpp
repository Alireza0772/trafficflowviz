#ifndef TFV_RENDERER_HPP
#define TFV_RENDERER_HPP

#include <cstdint>
#include <memory>
#include <string>

namespace tfv
{
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

        // Control anti-aliasing for renderers that support it
        virtual void setAntiAliasing(bool enable) = 0;

        // Access to underlying renderer for backend-specific operations
        virtual void* getNativeRenderer() const = 0;

        // Get window size
        virtual void getWindowSize(int& width, int& height) const = 0;
    };
} // namespace tfv
#endif
