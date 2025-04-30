#pragma once

#include "core/Event.hpp"
#include <iostream> // For debugging purposes
#include <string>
#include <utility> // For std::exchange
#include <variant>
#include <vector>

namespace tfv
{
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
} // namespace tfv