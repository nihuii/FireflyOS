#pragma once

#include <stdint.h>

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

class AlarmService {
public:
    static constexpr uint8_t kSlots = 2;

    bool set(uint8_t slot, const Alarm & alarm);
    const Alarm & get(uint8_t slot) const;
    AlarmTrigger nextTrigger(int64_t now) const;
    bool shouldTrigger(int64_t now, uint8_t & slot);

    static bool matchesWeekday(uint8_t days_mask, int tm_wday);

private:
    bool isActive(const Alarm & alarm) const;

    Alarm alarms_[kSlots]{};
    int64_t last_trigger_minute_[kSlots]{-1, -1};
};

}  // namespace firefly
