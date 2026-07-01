#include "ClockApp.h"

#include <stdio.h>
#include <time.h>

namespace firefly {
namespace {

constexpr int kScreenWidth = 410;
constexpr int kScreenHeight = 502;

void setLabel(lv_obj_t * label, const char * text) {
    if(label) {
        lv_label_set_text(label, text ? text : "");
    }
}

lv_obj_t * makeLabel(lv_obj_t * parent,
                     const char * text,
                     lv_color_t color,
                     const lv_font_t * font) {
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, color, 0);
    if(font) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    return label;
}

lv_obj_t * makeCard(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                    lv_coord_t w, lv_coord_t h, lv_color_t accent) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    UiComponents::styleSettingsCard(card, lv_color_hex(0x0D171D), 22, accent, LV_OPA_90);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

void formatDate(int64_t epoch_seconds, char * out, size_t out_size) {
    const time_t raw = static_cast<time_t>(epoch_seconds);
    struct tm local{};
    if(!localtime_r(&raw, &local)) {
        snprintf(out, out_size, "--/--");
        return;
    }

    static const char * weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    snprintf(out, out_size, "%02d/%02d  %s",
             local.tm_mon + 1, local.tm_mday, weekdays[local.tm_wday]);
}

void formatClock(int64_t epoch_seconds, char * out, size_t out_size) {
    const time_t raw = static_cast<time_t>(epoch_seconds);
    struct tm local{};
    if(!localtime_r(&raw, &local)) {
        snprintf(out, out_size, "--:--");
        return;
    }
    snprintf(out, out_size, "%02d:%02d", local.tm_hour, local.tm_min);
}

void formatAlarmTime(const Alarm & alarm, char * out, size_t out_size) {
    if(!alarm.configured) {
        snprintf(out, out_size, "--:--");
        return;
    }
    snprintf(out, out_size, "%02u:%02u",
             static_cast<unsigned>(alarm.hour),
             static_cast<unsigned>(alarm.minute));
}

}  // namespace

void CountdownTimer::start(uint32_t duration_ms, uint32_t now_ms) {
    running_ = duration_ms > 0;
    target_ms_ = now_ms + duration_ms;
}

void CountdownTimer::stop() {
    running_ = false;
    target_ms_ = 0;
}

uint32_t CountdownTimer::remainingMs(uint32_t now_ms) const {
    if(!running_) {
        return 0;
    }
    if(static_cast<int32_t>(target_ms_ - now_ms) <= 0) {
        return 0;
    }
    return target_ms_ - now_ms;
}

bool CountdownTimer::expired(uint32_t now_ms) const {
    return running_ && remainingMs(now_ms) == 0;
}

void StopwatchSession::start(int64_t now_us) {
    if(running_) {
        return;
    }
    running_ = true;
    started_at_us_ = now_us;
}

void StopwatchSession::pause(int64_t now_us) {
    if(!running_) {
        return;
    }
    accumulated_us_ = elapsedUs(now_us);
    running_ = false;
}

void StopwatchSession::reset() {
    running_ = false;
    started_at_us_ = 0;
    accumulated_us_ = 0;
}

int64_t StopwatchSession::elapsedUs(int64_t now_us) const {
    if(!running_) {
        return accumulated_us_;
    }
    const int64_t delta = now_us - started_at_us_;
    return accumulated_us_ + (delta > 0 ? delta : 0);
}

bool ClockApp::create(lv_obj_t * parent,
                      UiComponents & components,
                      TimeService & time,
                      AlarmService & alarms) {
    LV_UNUSED(components);
    if(!parent) {
        return false;
    }

    time_ = &time;
    alarms_ = &alarms;

    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, kScreenWidth, kScreenHeight);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x020607), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = makeLabel(root_, "Clock", lv_color_hex(0xEFFFFB), &lv_font_montserrat_24);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 30, 58);

    time_label_ = makeLabel(root_, "--:--", lv_color_hex(0xEFFFFB), &lv_font_montserrat_48);
    lv_obj_align(time_label_, LV_ALIGN_TOP_LEFT, 30, 104);

    date_label_ = makeLabel(root_, "RTC not ready", lv_color_hex(0x8BA6AA), nullptr);
    lv_obj_align(date_label_, LV_ALIGN_TOP_LEFT, 34, 170);

    lv_obj_t * next_card = makeCard(root_, 28, 210, 354, 72, lv_color_hex(0x62E8CA));
    next_alarm_label_ = makeLabel(next_card, "Next alarm  --:--", lv_color_hex(0xEFFFFB), nullptr);
    lv_obj_align(next_alarm_label_, LV_ALIGN_LEFT_MID, 0, 0);

    for(uint8_t slot = 0; slot < AlarmService::kSlots; ++slot) {
        lv_obj_t * card = makeCard(root_, 28, 296 + static_cast<lv_coord_t>(slot) * 68,
                                   170, 56, lv_color_hex(0x72CDE0));
        alarm_time_labels_[slot] = makeLabel(card, "--:--", lv_color_hex(0xEFFFFB), &lv_font_montserrat_24);
        lv_obj_align(alarm_time_labels_[slot], LV_ALIGN_LEFT_MID, 0, -7);
        alarm_detail_labels_[slot] = makeLabel(card, "Empty", lv_color_hex(0x8BA6AA), nullptr);
        lv_obj_align(alarm_detail_labels_[slot], LV_ALIGN_LEFT_MID, 0, 16);
    }

    lv_obj_t * timer_card = makeCard(root_, 212, 296, 170, 56, lv_color_hex(0xB0FF63));
    timer_label_ = makeLabel(timer_card, "Timer 10:00", lv_color_hex(0xEFFFFB), nullptr);
    lv_obj_center(timer_label_);

    lv_obj_t * stopwatch_card = makeCard(root_, 212, 364, 170, 56, lv_color_hex(0xFF9148));
    stopwatch_label_ = makeLabel(stopwatch_card, "Stopwatch 00:00", lv_color_hex(0xEFFFFB), nullptr);
    lv_obj_center(stopwatch_label_);

    timer_.start(10UL * 60UL * 1000UL, 0);
    hide();
    return true;
}

void ClockApp::destroy() {
    if(root_) {
        lv_obj_del(root_);
    }
    root_ = nullptr;
    time_label_ = nullptr;
    date_label_ = nullptr;
    next_alarm_label_ = nullptr;
    for(uint8_t slot = 0; slot < AlarmService::kSlots; ++slot) {
        alarm_time_labels_[slot] = nullptr;
        alarm_detail_labels_[slot] = nullptr;
    }
    timer_label_ = nullptr;
    stopwatch_label_ = nullptr;
}

void ClockApp::show() {
    if(root_) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ClockApp::hide() {
    if(root_) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ClockApp::refresh(const SystemState & state) {
    TimeSnapshot snapshot{};
    snapshot.valid = state.time.valid;
    snapshot.epoch_seconds = state.time.epoch_seconds;
    if(!snapshot.valid && time_) {
        snapshot = time_->now();
    }

    if(snapshot.valid) {
        char time_text[8];
        char date_text[24];
        formatClock(snapshot.epoch_seconds, time_text, sizeof(time_text));
        formatDate(snapshot.epoch_seconds, date_text, sizeof(date_text));
        setLabel(time_label_, time_text);
        setLabel(date_label_, date_text);
        refreshAlarmRows(snapshot.epoch_seconds);
    } else {
        setLabel(time_label_, "--:--");
        setLabel(date_label_, "RTC not ready");
        setLabel(next_alarm_label_, "Next alarm  --:--");
        refreshAlarmRows(0);
    }
}

void ClockApp::refreshAlarmRows(int64_t now_epoch) {
    if(!alarms_) {
        return;
    }

    const AlarmTrigger next = now_epoch > 0 ? alarms_->nextTrigger(now_epoch) : AlarmTrigger{};
    if(next.valid) {
        char next_time[8];
        formatClock(next.epoch_seconds, next_time, sizeof(next_time));
        char summary[40];
        snprintf(summary, sizeof(summary), "Next alarm  %s", next_time);
        setLabel(next_alarm_label_, summary);
    } else {
        setLabel(next_alarm_label_, "No alarms scheduled");
    }

    for(uint8_t slot = 0; slot < AlarmService::kSlots; ++slot) {
        const Alarm & alarm = alarms_->get(slot);
        char time_text[8];
        formatAlarmTime(alarm, time_text, sizeof(time_text));
        setLabel(alarm_time_labels_[slot], time_text);
        setLabel(alarm_detail_labels_[slot],
                 alarm.configured ? (alarm.enabled ? "Enabled" : "Off") : "Empty");
    }
}

}  // namespace firefly
