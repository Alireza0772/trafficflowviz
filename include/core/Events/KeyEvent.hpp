#pragma once

#include "core/Event.hpp"
#include <string>

namespace tfv
{
    /**
     * @brief Base class for all keyboard events
     */
    class KeyEvent : public Event
    {
    public:
        int getKeyCode() const { return m_keyCode; }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryKeyboard; }

    protected:
        KeyEvent(int keyCode) : m_keyCode(keyCode) {}

        int m_keyCode;
    };

    /**
     * @brief Event fired when a key is pressed
     */
    class KeyPressedEvent : public KeyEvent
    {
    public:
        KeyPressedEvent(int keyCode, bool isRepeat = false)
            : KeyEvent(keyCode), m_isRepeat(isRepeat)
        {
        }

        static EventType getStaticType() { return EventType::KeyPressed; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "KeyPressed"; }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryKeyboard; }

        bool isRepeat() const { return m_isRepeat; }

        std::string toString() const override
        {
            return "KeyPressedEvent: " + std::to_string(m_keyCode) + 
                   (m_isRepeat ? " (repeat)" : "");
        }

    private:
        bool m_isRepeat;
    };

    /**
     * @brief Event fired when a key is released
     */
    class KeyReleasedEvent : public KeyEvent
    {
    public:
        KeyReleasedEvent(int keyCode) : KeyEvent(keyCode) {}

        static EventType getStaticType() { return EventType::KeyReleased; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "KeyReleased"; }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryKeyboard; }

        std::string toString() const override
        {
            return "KeyReleasedEvent: " + std::to_string(m_keyCode);
        }
    };

    /**
     * @brief Event fired when a character is typed (for text input)
     */
    class KeyTypedEvent : public Event
    {
    public:
        KeyTypedEvent(const std::string& text) : m_text(text) {}
        KeyTypedEvent(char character) : m_text(1, character) {}
        KeyTypedEvent(uint32_t unicode) : m_unicode(unicode) 
        {
            // Convert unicode to UTF-8 string
            if (unicode <= 0x7F) {
                m_text = static_cast<char>(unicode);
            } else if (unicode <= 0x7FF) {
                m_text.resize(2);
                m_text[0] = 0xC0 | (unicode >> 6);
                m_text[1] = 0x80 | (unicode & 0x3F);
            } else if (unicode <= 0xFFFF) {
                m_text.resize(3);
                m_text[0] = 0xE0 | (unicode >> 12);
                m_text[1] = 0x80 | ((unicode >> 6) & 0x3F);
                m_text[2] = 0x80 | (unicode & 0x3F);
            } else {
                m_text.resize(4);
                m_text[0] = 0xF0 | (unicode >> 18);
                m_text[1] = 0x80 | ((unicode >> 12) & 0x3F);
                m_text[2] = 0x80 | ((unicode >> 6) & 0x3F);
                m_text[3] = 0x80 | (unicode & 0x3F);
            }
        }

        static EventType getStaticType() { return EventType::KeyTyped; }
        EventType getEventType() const override { return getStaticType(); }
        const char* getName() const override { return "KeyTyped"; }
        int getCategoryFlags() const override { return EventCategoryInput | EventCategoryKeyboard; }

        const std::string& getText() const { return m_text; }
        const char* getTextCStr() const { return m_text.c_str(); }
        uint32_t getUnicode() const { return m_unicode; }

        std::string toString() const override
        {
            return "KeyTypedEvent: '" + m_text + "' (U+" + 
                   std::to_string(m_unicode) + ")";
        }

    private:
        std::string m_text;
        uint32_t m_unicode = 0;
    };

} // namespace tfv
