#pragma once

#include "core/Event.hpp"
#include <iostream> // For debugging purposes
#include <string>
#include <utility> // For std::exchange
#include <variant>
#include <vector>

namespace tfv
{
    // WindowCloseEvent class definition
    class WindowCloseEvent : public Event
    {
      public:
        WindowCloseEvent() = default;

        EventType getEventType() const override { return EventType::WindowClose; }
        static EventType getStaticType() { return EventType::WindowClose; }
        const char* getName() const override { return "WindowClose"; }
        int getCategoryFlags() const override { return EventCategoryApplication; }
    };
    // WindowResizeEvent class definition
    class WindowResizeEvent : public Event
    {
      public:
        WindowResizeEvent(int width, int height) : m_width(width), m_height(height) {}

        int getWidth() const { return m_width; }
        int getHeight() const { return m_height; }

        std::string toString() const override
        {
            return "WindowResizeEvent: " + std::to_string(m_width) + " x " +
                   std::to_string(m_height);
        }
        EventType getEventType() const override { return EventType::WindowResize; }
        const char* getName() const override { return "WindowResize"; }
        int getCategoryFlags() const override
        {
            return EventCategoryApplication | EventCategoryInput;
        }

      private:
        int m_width;
        int m_height;
    };
    // WindowFocusEvent class definition
    class WindowFocusEvent : public Event
    {
      public:
        WindowFocusEvent() = default;

        EventType getEventType() const override { return EventType::WindowFocus; }
        const char* getName() const override { return "WindowFocus"; }
        int getCategoryFlags() const override { return EventCategoryApplication; }
    };
    // WindowLostFocusEvent class definition
    class WindowLostFocusEvent : public Event
    {
      public:
        WindowLostFocusEvent() = default;

        EventType getEventType() const override { return EventType::WindowLostFocus; }
        const char* getName() const override { return "WindowLostFocus"; }
        int getCategoryFlags() const override { return EventCategoryApplication; }
    };
    // WindowMovedEvent class definition
    class WindowMovedEvent : public Event
    {
      public:
        WindowMovedEvent(int x, int y) : m_x(x), m_y(y) {}

        int getX() const { return m_x; }
        int getY() const { return m_y; }

        std::string toString() const override
        {
            return "WindowMovedEvent: " + std::to_string(m_x) + ", " + std::to_string(m_y);
        }
        EventType getEventType() const override { return EventType::WindowMoved; }
        const char* getName() const override { return "WindowMoved"; }
        int getCategoryFlags() const override
        {
            return EventCategoryApplication | EventCategoryInput;
        }

      private:
        int m_x;
        int m_y;
    };
} // namespace tfv