#include "AlarmService.h"

#include <time.h>

namespace firefly {
namespace {

constexpr int16_t kTrailblaze[] = {
    0, 3212, 6392, 9512, 12539, 15446, 18204, 20787,
    23170, 25330, 27245, 28898, 30273, 31356, 32137, 32609,
    32767, 32609, 32137, 31356, 30273, 28898, 27245, 25330,
    23170, 20787, 18204, 15446, 12539, 9512, 6392, 3212,
    0, -3212, -6392, -9512, -12539, -15446, -18204, -20787,
    -23170, -25330, -27245, -28898, -30273, -31356, -32137, -32609,
    -32767, -32609, -32137, -31356, -30273, -28898, -27245, -25330,
    -23170, -20787, -18204, -15446, -12539, -9512, -6392, -3212,
};

constexpr int16_t kStarglow[] = {
    0, 6392, 12539, 18204, 23170, 27245, 30273, 32137,
    32767, 32137, 30273, 27245, 23170, 18204, 12539, 6392,
    0, -6392, -12539, -18204, -23170, -27245, -30273, -32137,
    -32767, -32137, -30273, -27245, -23170, -18204, -12539, -6392,
};

constexpr int16_t kNightSky[] = {
    0, 1606, 3212, 4796, 6392, 7950, 9512, 11030,
    12539, 13999, 15446, 16834, 18204, 19510, 20787, 21995,
    23170, 24266, 25330, 26310, 27245, 28087, 28898, 29604,
    30273, 30831, 31356, 31770, 32137, 32397, 32609, 32714,
    32767, 32714, 32609, 32397, 32137, 31770, 31356, 30831,
    30273, 29604, 28898, 28087, 27245, 26310, 25330, 24266,
    23170, 21995, 20787, 19510, 18204, 16834, 15446, 13999,
    12539, 11030, 9512, 7950, 6392, 4796, 3212, 1606,
    0, -1606, -3212, -4796, -6392, -7950, -9512, -11030,
    -12539, -13999, -15446, -16834, -18204, -19510, -20787, -21995,
    -23170, -24266, -25330, -26310, -27245, -28087, -28898, -29604,
    -30273, -30831, -31356, -31770, -32137, -32397, -32609, -32714,
    -32767, -32714, -32609, -32397, -32137, -31770, -31356, -30831,
    -30273, -29604, -28898, -28087, -27245, -26310, -25330, -24266,
    -23170, -21995, -20787, -19510, -18204, -16834, -15446, -13999,
    -12539, -11030, -9512, -7950, -6392, -4796, -3212, -1606,
};

constexpr int16_t kClassicBell[] = {
    0, 20000, 28000, 22000, 8000, -10000, -22000, -26000,
    -18000, -2000, 14000, 21000, 16000, 4000, -9000, -15000,
    -11000, -1000, 8000, 12000, 9000, 2000, -5000, -8000,
    -6000, -1000, 4000, 6000, 4000, 1000, -2500, -3500,
};

constexpr AlarmToneResource kRingtones[] = {
    {"Trailblaze", kTrailblaze,
     sizeof(kTrailblaze) / sizeof(kTrailblaze[0]), 16000, true},
    {"Starglow", kStarglow,
     sizeof(kStarglow) / sizeof(kStarglow[0]), 16000, true},
    {"Night Sky", kNightSky,
     sizeof(kNightSky) / sizeof(kNightSky[0]), 16000, true},
    {"Classic Bell", kClassicBell,
     sizeof(kClassicBell) / sizeof(kClassicBell[0]), 16000, true},
};

static_assert(sizeof(kRingtones) / sizeof(kRingtones[0]) ==
                  AlarmService::kRingtoneCount,
              "ringtone table must keep the public four-tone index");
static_assert(sizeof(kNightSky) / sizeof(kNightSky[0]) <=
                  AlarmService::kMaximumRingtoneFrames,
              "ringtone exceeds the twenty-second PCM budget");

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
    if(!findDueSlot(now, slot)) return false;
    last_trigger_minute_[slot] = now / 60;
    return true;
}

bool AlarmService::publishTrigger(int64_t now,
                                  uint32_t timestamp_ms,
                                  EventBus & events) {
    uint8_t slot = 0;
    if(!findDueSlot(now, slot)) return false;
    const SystemEvent event(EventType::AlarmTriggered, slot, timestamp_ms,
                            EventPriority::Critical);
    if(!events.post(event)) return false;
    last_trigger_minute_[slot] = now / 60;
    return true;
}

bool AlarmService::findDueSlot(int64_t now, uint8_t & slot) const {
    const time_t now_ts = static_cast<time_t>(now);
    struct tm now_tm;
    if(!localtime_r(&now_ts, &now_tm)) return false;
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

        slot = alarm_slot;
        return true;
    }

    return false;
}

void AlarmService::resetTriggerHistory() {
    for(uint8_t slot = 0; slot < kSlots; ++slot) {
        last_trigger_minute_[slot] = -1;
    }
}

bool AlarmService::matchesWeekday(uint8_t days_mask, int tm_wday) {
    return (days_mask & weekdayMaskForTmWday(tm_wday)) != 0;
}

const AlarmToneResource & AlarmService::ringtoneResource(uint8_t index) {
    if(index >= kRingtoneCount) index = 0;
    return kRingtones[index];
}

bool AlarmService::isActive(const Alarm & alarm) const {
    return alarm.configured && alarm.enabled && alarm.days_mask != 0;
}

}  // namespace firefly
