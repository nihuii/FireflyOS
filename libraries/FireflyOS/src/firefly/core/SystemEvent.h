#pragma once

#include <stdint.h>

namespace firefly {

enum class EventType : uint8_t {
    None,
    ShortPress,
    PowerPress,
    EnterSleep,
    SleepBlackout,
    Wake,
    TimeChanged,
    BatteryChanged,
    ChargingChanged,
    AlarmTriggered,
    TimerExpired,
    SdRemoved,
    CapabilityChanged,
    AppOpenRequested,
    AppCloseRequested
};

enum class EventPriority : uint8_t {
    Refresh,
    Normal,
    Critical
};

struct SystemEvent {
    EventType type;
    uint32_t value;
    uint32_t timestamp_ms;
    EventPriority priority;

    SystemEvent()
        : type(EventType::None),
          value(0),
          timestamp_ms(0),
          priority(EventPriority::Normal) {}

    SystemEvent(EventType event_type,
                uint32_t event_value,
                uint32_t event_timestamp_ms,
                EventPriority event_priority = EventPriority::Normal)
        : type(event_type),
          value(event_value),
          timestamp_ms(event_timestamp_ms),
          priority(event_priority) {}
};

}  // namespace firefly
