// --- Event.hpp ---
#ifndef TFV_EVENT_HPP
#define TFV_EVENT_HPP

#include <functional>
#include <string>

namespace tfv
{
    // Define different event types
    enum class EventType
    {
        None = 0,
        WindowClose,
        WindowResize,
        WindowFocus,
        WindowLostFocus,
        WindowMoved,
        AppTick,
        AppUpdate,
        AppRender,
        KeyPressed,
        KeyReleased,
        KeyTyped, // KeyTyped for character input
        MouseButtonPressed,
        MouseButtonReleased,
        MouseMoved,
        MouseScrolled
    };

    // Define event categories (optional but useful for filtering)
    enum EventCategory
    {
        None = 0,
        EventCategoryApplication = (1 << 0),
        EventCategoryInput = (1 << 1),
        EventCategoryKeyboard = (1 << 2),
        EventCategoryMouse = (1 << 3),
        EventCategoryMouseButton = (1 << 4)
    };

    // Base Event class
    class Event
    {
      public:
        virtual ~Event() = default;

        bool handled = false; // To allow layers to consume events

        virtual EventType getEventType() const = 0;
        virtual const char* getName() const = 0;
        virtual int getCategoryFlags() const = 0;
        virtual std::string toString() const { return getName(); }

        bool isInCategory(EventCategory category) { return getCategoryFlags() & category; }
    };

    // Helper for dispatching events based on type
    class EventDispatcher
    {
      public:
        EventDispatcher(Event& event) : m_event(event) {}

        template <typename T, typename F> bool dispatch(const F& func)
        {
            if(m_event.getEventType() == T::getStaticType())
            {
                m_event.handled |= func(static_cast<T&>(m_event));
                return true;
            }
            return false;
        }

      private:
        Event& m_event;
    };

    // Example: WindowCloseEvent (define others similarly)
    class WindowCloseEvent : public Event
    {
      public:
        WindowCloseEvent() = default;

        EventType getEventType() const override { return EventType::WindowClose; }
        static EventType getStaticType() { return EventType::WindowClose; }
        const char* getName() const override { return "WindowClose"; }
        int getCategoryFlags() const override { return EventCategoryApplication; }
    };

    // --- Add other event classes: WindowResizeEvent, KeyPressedEvent, etc. ---
    // Example KeyEvent:
    class KeyEvent : public Event
    {
      public:
        int getKeyCode() const { return m_keyCode; }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryKeyboard; }

      protected:
        KeyEvent(int keyCode) : m_keyCode(keyCode) {}

        int m_keyCode;
    };
    class KeyPressedEvent : public KeyEvent
    {
      public:
        KeyPressedEvent(int keyCode, int repeatCount)
            : KeyEvent(keyCode), m_repeatCount(repeatCount)
        {
        }

        EventType getEventType() const override { return EventType::KeyPressed; }
        static EventType getStaticType() { return EventType::KeyPressed; }
        const char* getName() const override { return "KeyPressed"; }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryKeyboard; }

        int getKeyCode() const { return m_keyCode; }
        int getRepeatCount() const { return m_repeatCount; }

      private:
        int m_keyCode;
        int m_repeatCount;
    };
    class KeyReleasedEvent : public KeyEvent
    {
      public:
        KeyReleasedEvent(int keyCode) : KeyEvent(keyCode) {}

        EventType getEventType() const override { return EventType::KeyReleased; }
        static EventType getStaticType() { return EventType::KeyReleased; }
        const char* getName() const override { return "KeyReleased"; }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryKeyboard; }

        int getKeyCode() const { return m_keyCode; }

      private:
        int m_keyCode;
    };
    class KeyTypedEvent : public KeyEvent
    {
      public:
        KeyTypedEvent(const char* text) : KeyEvent(0), m_text(text) {}

        EventType getEventType() const override { return EventType::KeyTyped; }
        static EventType getStaticType() { return EventType::KeyTyped; }
        const char* getName() const override { return "KeyTyped"; }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryKeyboard; }

        const char* getText() const { return m_text.c_str(); }

      private:
        std::string m_text;
        int m_keyCode; // Optional: if you want to keep track of the key code
    };
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
    class MouseMovedEvent : public MouseEvent
    {
      public:
        MouseMovedEvent(int x, int y) : MouseEvent(0, x, y) {}

        EventType getEventType() const override { return EventType::MouseMoved; }
        static EventType getStaticType() { return EventType::MouseMoved; }
        const char* getName() const override { return "MouseMoved"; }
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

} // namespace tfv
#endif // TFV_EVENT_HPP