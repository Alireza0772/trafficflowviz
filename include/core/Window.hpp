#ifndef TFV_IWINDOW_HPP
#define TFV_IWINDOW_HPP

#include <functional>
#include <memory> // For unique_ptr
#include <string>

// Forward declare your abstract Event class (see step 2)
namespace tfv
{
    class Event;
}

namespace tfv
{
    // Callback function type for events
    using EventCallbackFn = std::function<void(Event&)>;

    struct WindowProps
    {
        std::string title;
        int width;
        int height;
        // Add other properties like fullscreen, vsync, etc. if needed
    };

    class Window
    {
      public:
        virtual ~Window() = default;

        virtual bool initialize(const WindowProps& props) = 0;
        virtual void shutdown() = 0;

        virtual void pollEvents() = 0;  // Polls and dispatches events via callback
        virtual void swapBuffers() = 0; // For double-buffered rendering

        virtual int getWidth() const = 0;
        virtual int getHeight() const = 0;
        virtual void setSize(int width, int height) = 0; // Optional: if resizing supported

        virtual void setEventCallback(const EventCallbackFn& callback) = 0;

        virtual void* getNativeWindow() const = 0; // To pass to underlying APIs if needed

        // --- ImGui Platform Backend Abstraction ---
        virtual void initImGuiPlatform() = 0;
        virtual void shutdownImGuiPlatform() = 0;
        virtual void newFrameImGuiPlatform() = 0;
        // Processing events might be handled within pollEvents or need a dedicated function
        // depending on your event system design. Let's assume pollEvents handles it for now.

        static std::unique_ptr<Window> create(const WindowProps& props = {"TrafficVis", 1280,
                                                                          720}); // Factory
    };

} // namespace tfv

#endif // TFV_IWINDOW_HPP