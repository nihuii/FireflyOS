#include "FireflyAlarm.h"

#include <string.h>

namespace {

constexpr uint8_t EVERYDAY_MASK = 0x7F;
constexpr uint8_t WEEKDAY_MASK = 0x3E;
constexpr uint8_t WEEKEND_MASK = 0x41;
constexpr uint8_t DAY_MASKS[] = {
    EVERYDAY_MASK,
    WEEKDAY_MASK,
    WEEKEND_MASK,
    0x02,
    0x04,
    0x08,
    0x10,
    0x20,
    0x40,
    0x01
};

constexpr const char * DAY_LABELS[] = {
    "Every day",
    "Weekdays",
    "Weekend",
    "Mon",
    "Tue",
    "Wed",
    "Thu",
    "Fri",
    "Sat",
    "Sun"
};

constexpr const char * RINGTONE_NAMES[] = {
    "Trailblaze",
    "Starglow",
    "Night Sky",
    "Classic Bell"
};

constexpr char DAY_OPTIONS_TEXT[] =
    "Every day\nWeekdays\nWeekend\nMonday\nTuesday\nWednesday\nThursday\nFriday\nSaturday\nSunday";

constexpr char RINGTONE_OPTIONS_TEXT[] =
    "Trailblaze\nStarglow\nNight Sky\nClassic Bell";

bool is_alarm_active(const FireflyAlarm & alarm) {
    return alarm.configured && alarm.enabled && alarm.days_mask != 0;
}

uint8_t weekday_mask_for_tm_wday(int tm_wday) {
    if(tm_wday < 0 || tm_wday > 6) {
        return 0;
    }

    return static_cast<uint8_t>(1U << tm_wday);
}

} // namespace

FireflyAlarm firefly_alarms[FIREFLY_ALARM_SLOT_COUNT];
String firefly_alarm_last_trigger_keys[FIREFLY_ALARM_SLOT_COUNT];

void firefly_alarm_reset(FireflyAlarm & alarm, uint8_t slot_index) {
    alarm.configured = false;
    alarm.enabled = false;
    alarm.hour = 7;
    alarm.minute = 30;
    alarm.days_mask = EVERYDAY_MASK;
    alarm.ringtone_index = 0;
    snprintf(alarm.name, sizeof(alarm.name), "Alarm %u", static_cast<unsigned>(slot_index + 1U));
}

const char * firefly_alarm_ringtone_name(uint8_t index) {
    if(index >= FIREFLY_ALARM_RINGTONE_COUNT) {
        index = 0;
    }
    return RINGTONE_NAMES[index];
}

const char * firefly_alarm_day_label(uint8_t days_mask) {
    for(uint8_t i = 0; i < sizeof(DAY_MASKS) / sizeof(DAY_MASKS[0]); ++i) {
        if(DAY_MASKS[i] == days_mask) {
            return DAY_LABELS[i];
        }
    }
    return "Custom";
}

const char * firefly_alarm_day_options_text() {
    return DAY_OPTIONS_TEXT;
}

const char * firefly_alarm_ringtone_options_text() {
    return RINGTONE_OPTIONS_TEXT;
}

uint8_t firefly_alarm_days_mask_from_option(uint8_t option_index) {
    if(option_index >= sizeof(DAY_MASKS) / sizeof(DAY_MASKS[0])) {
        option_index = 0;
    }
    return DAY_MASKS[option_index];
}

uint8_t firefly_alarm_option_from_days_mask(uint8_t days_mask) {
    for(uint8_t i = 0; i < sizeof(DAY_MASKS) / sizeof(DAY_MASKS[0]); ++i) {
        if(DAY_MASKS[i] == days_mask) {
            return i;
        }
    }
    return 0;
}

bool firefly_alarm_matches_weekday(uint8_t days_mask, int tm_wday) {
    return (days_mask & weekday_mask_for_tm_wday(tm_wday)) != 0;
}

bool firefly_alarm_find_next(time_t now_ts, int & out_slot, time_t & out_trigger_ts) {
    bool found = false;
    time_t earliest = 0;
    int earliest_slot = -1;

    for(uint8_t slot = 0; slot < FIREFLY_ALARM_SLOT_COUNT; ++slot) {
        const FireflyAlarm & alarm = firefly_alarms[slot];
        if(!is_alarm_active(alarm)) {
            continue;
        }

        for(uint8_t offset = 0; offset < 8; ++offset) {
            const time_t day_ts = now_ts + static_cast<time_t>(offset) * 86400;
            struct tm candidate_tm;
            localtime_r(&day_ts, &candidate_tm);

            if(!firefly_alarm_matches_weekday(alarm.days_mask, candidate_tm.tm_wday)) {
                continue;
            }

            candidate_tm.tm_hour = alarm.hour;
            candidate_tm.tm_min = alarm.minute;
            candidate_tm.tm_sec = 0;

            const time_t candidate_ts = mktime(&candidate_tm);
            if(candidate_ts <= now_ts) {
                continue;
            }

            if(!found || candidate_ts < earliest) {
                found = true;
                earliest = candidate_ts;
                earliest_slot = slot;
            }
            break;
        }
    }

    if(!found) {
        out_slot = -1;
        out_trigger_ts = 0;
        return false;
    }

    out_slot = earliest_slot;
    out_trigger_ts = earliest;
    return true;
}

String firefly_alarm_minutes_until_text(time_t now_ts) {
    int slot = -1;
    time_t trigger_ts = 0;
    if(!firefly_alarm_find_next(now_ts, slot, trigger_ts)) {
        return "No alarms scheduled";
    }

    const unsigned long diff_seconds = static_cast<unsigned long>(trigger_ts - now_ts);
    const unsigned long diff_minutes = (diff_seconds + 59UL) / 60UL;
    return "Next alarm in " + String(diff_minutes) + " min";
}
