#pragma once
#include "core/Event.hpp"
#include <string>

namespace tfv
{
    // Mouse button constants
    enum class MouseButton
    {
        Left = 0,
        Right = 1,
        Middle = 2,
        Button4 = 3,
        Button5 = 4
    };

    /**
     * @brief Base class for all mouse events
     */
    class MouseEvent : public Event
    {
    public:
        float getX() const { return m_x; }
        float getY() const { return m_y; }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryMouse; }

    protected:
        MouseEvent(float x, float y) : m_x(x), m_y(y) {}

        float m_x, m_y;
    };

    /**
     * @brief Event fired when the mouse is moved
     */
    class MouseMovedEvent : public MouseEvent
    {
    public:
        MouseMovedEvent(float x, float y) : MouseEvent(x, y) {}

        static EventType getStaticType() { return EventType::MouseMoved; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "MouseMoved"; }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryMouse; }

        std::string toString() const override
        {
            return "MouseMovedEvent: " + std::to_string(m_x) + ", " + std::to_string(m_y);
        }
    };

    /**
     * @brief Event fired when the mouse wheel is scrolled
     */
    class MouseScrolledEvent : public MouseEvent
    {
    public:
        MouseScrolledEvent(float xOffset, float yOffset, float x = 0.0f, float y = 0.0f)
            : MouseEvent(x, y), m_xOffset(xOffset), m_yOffset(yOffset)
        {
        }

        static EventType getStaticType() { return EventType::MouseScrolled; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "MouseScrolled"; }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryMouse; }

        float getXOffset() const { return m_xOffset; }
        float getYOffset() const { return m_yOffset; }

        std::string toString() const override
        {
            return "MouseScrolledEvent: " + std::to_string(m_xOffset) + ", " + std::to_string(m_yOffset);
        }

    private:
        float m_xOffset, m_yOffset;
    };

    /**
     * @brief Base class for mouse button events
     */
    class MouseButtonEvent : public MouseEvent
    {
    public:
        MouseButton getMouseButton() const { return m_button; }
        int getButtonCode() const { return static_cast<int>(m_button); }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryMouse | EventCategoryMouseButton; }

    protected:
        MouseButtonEvent(MouseButton button, float x, float y) 
            : MouseEvent(x, y), m_button(button) {}

        MouseButton m_button;
    };

    /**
     * @brief Event fired when a mouse button is pressed
     */
    class MouseButtonPressedEvent : public MouseButtonEvent
    {
    public:
        MouseButtonPressedEvent(MouseButton button, float x, float y) 
            : MouseButtonEvent(button, x, y) {}

        static EventType getStaticType() { return EventType::MouseButtonPressed; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "MouseButtonPressed"; }

        std::string toString() const override
        {
            return "MouseButtonPressedEvent: " + std::to_string(static_cast<int>(m_button)) +
                   " at (" + std::to_string(m_x) + ", " + std::to_string(m_y) + ")";
        }
    };

    /**
     * @brief Event fired when a mouse button is released
     */
    class MouseButtonReleasedEvent : public MouseButtonEvent
    {
    public:
        MouseButtonReleasedEvent(MouseButton button, float x, float y) 
            : MouseButtonEvent(button, x, y) {}

        static EventType getStaticType() { return EventType::MouseButtonReleased; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "MouseButtonReleased"; }

        std::string toString() const override
        {
            return "MouseButtonReleasedEvent: " + std::to_string(static_cast<int>(m_button)) +
                   " at (" + std::to_string(m_x) + ", " + std::to_string(m_y) + ")";
        }
    };

    // Convenience functions for mouse button names
    inline const char* getMouseButtonName(MouseButton button)
    {
        switch (button)
        {
        case MouseButton::Left:   return "Left";
        case MouseButton::Right:  return "Right";
        case MouseButton::Middle: return "Middle";
        case MouseButton::Button4: return "Button4";
        case MouseButton::Button5: return "Button5";
        default: return "Unknown";
        }
    }

} // namespace tfv
