#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../core/EventBus.h"

namespace firefly {

struct Alarm {
    bool configured = false;
    bool enabled = false;
    uint8_t hour = 7;
    uint8_t minute = 30;
    uint8_t days_mask = 0x7F;
    uint8_t ringtone = 0;
    char name[24] = "Alarm";
};

struct AlarmTrigger {
    bool valid = false;
    uint8_t slot = 0;
    int64_t epoch_seconds = 0;
};

struct AlarmToneResource {
    constexpr AlarmToneResource(const char * resource_name = nullptr,
                                const int16_t * resource_samples = nullptr,
                                size_t resource_frames = 0,
                                uint32_t resource_rate = 16000,
                                bool resource_loop = true)
        : name(resource_name), samples(resource_samples),
          frames(resource_frames), sample_rate(resource_rate),
          loop(resource_loop) {}
    const char * name = nullptr;
    const int16_t * samples = nullptr;
    size_t frames = 0;
    uint32_t sample_rate = 16000;
    bool loop = true;
};

class AlarmService {
public:
    static constexpr uint8_t kSlots = 2;
    static constexpr uint8_t kRingtoneCount = 4;
    static constexpr size_t kMaximumRingtoneFrames = 320000;

    bool set(uint8_t slot, const Alarm & alarm);
    const Alarm & get(uint8_t slot) const;
    AlarmTrigger nextTrigger(int64_t now) const;
    bool shouldTrigger(int64_t now, uint8_t & slot);
    bool publishTrigger(int64_t now, uint32_t timestamp_ms, EventBus & events);
    void resetTriggerHistory();

    static bool matchesWeekday(uint8_t days_mask, int tm_wday);
    static const AlarmToneResource & ringtoneResource(uint8_t index);

private:
    bool isActive(const Alarm & alarm) const;
    bool findDueSlot(int64_t now, uint8_t & slot) const;

    Alarm alarms_[kSlots]{};
    int64_t last_trigger_minute_[kSlots]{-1, -1};
};

}  // namespace firefly
