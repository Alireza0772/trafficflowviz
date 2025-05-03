#pragma once
#include "core/Event.hpp"
namespace tfv
{
    class MouseEvent : public Event
    {
      public:
        int getX() const { return m_x; }
        int getY() const { return m_y; }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryMouse; }

      protected:
        MouseEvent(int button, int x, int y) : m_button(button), m_x(x), m_y(y) {}
        int m_button;
        int m_x, m_y;
    };

    class MouseScrolledEvent : public MouseEvent
    {
      public:
        MouseScrolledEvent(float xOffset, float yOffset)
            : MouseEvent(0, (int)xOffset, (int)yOffset), m_xOffset(xOffset), m_yOffset(yOffset)
        {
        }

        EventType getEventType() const override { return EventType::MouseScrolled; }
        static EventType getStaticType() { return EventType::MouseScrolled; }
        const char* getName() const override { return "MouseScrolled"; }

        float getXOffset() const { return m_xOffset; }
        float getYOffset() const { return m_yOffset; }

      private:
        float m_xOffset, m_yOffset;
    };
    class MouseMovedEvent : public MouseEvent
    {
      public:
        MouseMovedEvent(int x, int y) : MouseEvent(0, x, y) {}

        EventType getEventType() const override { return EventType::MouseMoved; }
        static EventType getStaticType() { return EventType::MouseMoved; }
        const char* getName() const override { return "MouseMoved"; }
    };

    class MouseButtonPressedEvent : public MouseEvent
    {
      public:
        MouseButtonPressedEvent(int button, int x, int y) : MouseEvent(button, x, y) {}

        EventType getEventType() const override { return EventType::MouseButtonPressed; }
        static EventType getStaticType() { return EventType::MouseButtonPressed; }
        const char* getName() const override { return "MouseButtonPressed"; }
    };
    class MouseButtonReleasedEvent : public MouseEvent
    {
      public:
        MouseButtonReleasedEvent(int button, int x, int y) : MouseEvent(button, x, y) {}

        EventType getEventType() const override { return EventType::MouseButtonReleased; }
        static EventType getStaticType() { return EventType::MouseButtonReleased; }
        const char* getName() const override { return "MouseButtonReleased"; }
    };
} // namespace tfv
