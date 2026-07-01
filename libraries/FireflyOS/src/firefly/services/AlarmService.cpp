#include "AlarmService.h"

#include <time.h>

namespace firefly {
namespace {

uint8_t weekdayMaskForTmWday(int tm_wday) {
    if(tm_wday < 0 || tm_wday > 6) {
        return 0;
    }

    return static_cast<uint8_t>(1U << tm_wday);
}

}  // namespace

bool AlarmService::set(uint8_t slot, const Alarm & alarm) {
    if(slot >= kSlots || alarm.hour > 23 || alarm.minute > 59) {
        return false;
    }

    alarms_[slot] = alarm;
    alarms_[slot].days_mask = static_cast<uint8_t>(alarm.days_mask & 0x7F);
    return true;
}

const Alarm & AlarmService::get(uint8_t slot) const {
    if(slot >= kSlots) {
        slot = 0;
    }
    return alarms_[slot];
}

AlarmTrigger AlarmService::nextTrigger(int64_t now) const {
    bool found = false;
    AlarmTrigger earliest{};

    for(uint8_t alarm_slot = 0; alarm_slot < kSlots; ++alarm_slot) {
        const Alarm & alarm = alarms_[alarm_slot];
        if(!isActive(alarm)) {
            continue;
        }

        for(uint8_t offset = 0; offset < 8; ++offset) {
            const time_t day_ts = static_cast<time_t>(now + static_cast<int64_t>(offset) * 86400);
            struct tm candidate_tm;
            localtime_r(&day_ts, &candidate_tm);

            if(!matchesWeekday(alarm.days_mask, candidate_tm.tm_wday)) {
                continue;
            }

            candidate_tm.tm_hour = alarm.hour;
            candidate_tm.tm_min = alarm.minute;
            candidate_tm.tm_sec = 0;

            const time_t candidate_ts = mktime(&candidate_tm);
            if(static_cast<int64_t>(candidate_ts) <= now) {
                continue;
            }

            if(!found || static_cast<int64_t>(candidate_ts) < earliest.epoch_seconds) {
                found = true;
                earliest.valid = true;
                earliest.slot = alarm_slot;
                earliest.epoch_seconds = static_cast<int64_t>(candidate_ts);
            }
            break;
        }
    }

    return earliest;
}

bool AlarmService::shouldTrigger(int64_t now, uint8_t & slot) {
    const time_t now_ts = static_cast<time_t>(now);
    struct tm now_tm;
    localtime_r(&now_ts, &now_tm);
    const int64_t minute_key = now / 60;

    for(uint8_t alarm_slot = 0; alarm_slot < kSlots; ++alarm_slot) {
        const Alarm & alarm = alarms_[alarm_slot];
        if(!isActive(alarm)) {
            continue;
        }
        if(alarm.hour != static_cast<uint8_t>(now_tm.tm_hour) ||
           alarm.minute != static_cast<uint8_t>(now_tm.tm_min)) {
            continue;
        }
        if(!matchesWeekday(alarm.days_mask, now_tm.tm_wday)) {
            continue;
        }
        if(last_trigger_minute_[alarm_slot] == minute_key) {
            continue;
        }

        last_trigger_minute_[alarm_slot] = minute_key;
        slot = alarm_slot;
        return true;
    }

    return false;
}

bool AlarmService::matchesWeekday(uint8_t days_mask, int tm_wday) {
    return (days_mask & weekdayMaskForTmWday(tm_wday)) != 0;
}

bool AlarmService::isActive(const Alarm & alarm) const {
    return alarm.configured && alarm.enabled && alarm.days_mask != 0;
}

}  // namespace firefly
