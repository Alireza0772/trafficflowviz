#pragma once

#include "core/Event.hpp"

namespace tfv
{
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
} // namespace tfv
