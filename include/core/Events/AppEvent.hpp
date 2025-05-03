#pragma once
#include "core/Event.hpp"

namespace tfv
{
    // App Tick Event
    class AppTickEvent : public Event
    {
      public:
        AppTickEvent() = default;

        EventType getEventType() const override { return EventType::AppTick; }
        const char* getName() const override { return "AppTick"; }
        int getCategoryFlags() const override { return EventCategory::EventCategoryApplication; }
    };
    // App Update Event
    class AppUpdateEvent : public Event
    {
      public:
        AppUpdateEvent() = default;

        EventType getEventType() const override { return EventType::AppUpdate; }
        const char* getName() const override { return "AppUpdate"; }
        int getCategoryFlags() const override { return EventCategory::EventCategoryApplication; }
    };
    // App Render Event
    class AppRenderEvent : public Event
    {
      public:
        AppRenderEvent() = default;

        EventType getEventType() const override { return EventType::AppRender; }
        const char* getName() const override { return "AppRender"; }
        int getCategoryFlags() const override { return EventCategory::EventCategoryApplication; }
    };
} // namespace tfv
