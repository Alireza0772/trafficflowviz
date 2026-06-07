#pragma once
#include "core/Event.hpp"

namespace tfv
{
    /**
     * @brief Event fired when the application window is closed
     */
    class WindowCloseEvent : public Event
    {
    public:
        WindowCloseEvent() = default;

        static EventType getStaticType() { return EventType::WindowClose; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "WindowClose"; }
        int getCategoryFlags() const override { return EventCategoryApplication; }
    };

    /**
     * @brief Event fired when the application window is resized
     */
    class WindowResizeEvent : public Event
    {
    public:
        WindowResizeEvent(int width, int height) : m_width(width), m_height(height) {}

        int getWidth() const { return m_width; }
        int getHeight() const { return m_height; }

        static EventType getStaticType() { return EventType::WindowResize; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "WindowResize"; }
        int getCategoryFlags() const override { return EventCategoryApplication; }
        
        std::string toString() const override
        {
            return "WindowResizeEvent: " + std::to_string(m_width) + ", " + std::to_string(m_height);
        }

    private:
        int m_width, m_height;
    };

    /**
     * @brief Event fired when the application window gains focus
     */
    class WindowFocusEvent : public Event
    {
    public:
        WindowFocusEvent() = default;

        static EventType getStaticType() { return EventType::WindowFocus; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "WindowFocus"; }
        int getCategoryFlags() const override { return EventCategoryApplication; }
    };

    /**
     * @brief Event fired when the application window loses focus
     */
    class WindowLostFocusEvent : public Event
    {
    public:
        WindowLostFocusEvent() = default;

        static EventType getStaticType() { return EventType::WindowLostFocus; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "WindowLostFocus"; }
        int getCategoryFlags() const override { return EventCategoryApplication; }
    };

    /**
     * @brief Event fired when the application window is moved
     */
    class WindowMovedEvent : public Event
    {
    public:
        WindowMovedEvent(int x, int y) : m_x(x), m_y(y) {}

        int getX() const { return m_x; }
        int getY() const { return m_y; }

        static EventType getStaticType() { return EventType::WindowMoved; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "WindowMoved"; }
        int getCategoryFlags() const override { return EventCategoryApplication; }
        
        std::string toString() const override
        {
            return "WindowMovedEvent: " + std::to_string(m_x) + ", " + std::to_string(m_y);
        }

    private:
        int m_x, m_y;
    };

    /**
     * @brief Event fired each application tick for update logic
     */
    class AppTickEvent : public Event
    {
    public:
        AppTickEvent() = default;

        static EventType getStaticType() { return EventType::AppTick; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "AppTick"; }
        int getCategoryFlags() const override { return EventCategoryApplication; }
    };

    /**
     * @brief Event fired each application frame for update logic
     */
    class AppUpdateEvent : public Event
    {
    public:
        AppUpdateEvent(float deltaTime) : m_deltaTime(deltaTime) {}

        float getDeltaTime() const { return m_deltaTime; }

        static EventType getStaticType() { return EventType::AppUpdate; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "AppUpdate"; }
        int getCategoryFlags() const override { return EventCategoryApplication; }
        
        std::string toString() const override
        {
            return "AppUpdateEvent: dt=" + std::to_string(m_deltaTime);
        }

    private:
        float m_deltaTime;
    };

    /**
     * @brief Event fired each application frame for rendering
     */
    class AppRenderEvent : public Event
    {
    public:
        AppRenderEvent() = default;

        static EventType getStaticType() { return EventType::AppRender; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "AppRender"; }
        int getCategoryFlags() const override { return EventCategoryApplication; }
    };

} // namespace tfv
