#pragma once

#include <Arduino.h>
#include <time.h>

constexpr uint8_t FIREFLY_ALARM_SLOT_COUNT = 2;
constexpr uint8_t FIREFLY_ALARM_RINGTONE_COUNT = 4;

struct FireflyAlarm {
    bool configured;
    bool enabled;
    uint8_t hour;
    uint8_t minute;
    uint8_t days_mask;
    uint8_t ringtone_index;
    char name[24];
};

extern FireflyAlarm firefly_alarms[FIREFLY_ALARM_SLOT_COUNT];
extern String firefly_alarm_last_trigger_keys[FIREFLY_ALARM_SLOT_COUNT];

void firefly_alarm_reset(FireflyAlarm & alarm, uint8_t slot_index);
const char * firefly_alarm_ringtone_name(uint8_t index);
const char * firefly_alarm_day_label(uint8_t days_mask);
const char * firefly_alarm_day_options_text();
const char * firefly_alarm_ringtone_options_text();
uint8_t firefly_alarm_days_mask_from_option(uint8_t option_index);
uint8_t firefly_alarm_option_from_days_mask(uint8_t days_mask);
bool firefly_alarm_matches_weekday(uint8_t days_mask, int tm_wday);
bool firefly_alarm_find_next(time_t now_ts, int & out_slot, time_t & out_trigger_ts);
String firefly_alarm_minutes_until_text(time_t now_ts);
