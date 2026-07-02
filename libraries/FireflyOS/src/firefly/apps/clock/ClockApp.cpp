#include "ClockApp.h"

#include <esp_timer.h>
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

lv_obj_t * makePage(lv_obj_t * parent) {
    lv_obj_t * page = lv_obj_create(parent);
    lv_obj_set_size(page, kScreenWidth, kScreenHeight);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

lv_obj_t * makeButton(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                      lv_coord_t width, const char * text,
                      lv_event_cb_t callback, void * user_data,
                      lv_obj_t ** label_out = nullptr) {
    lv_obj_t * button = lv_btn_create(parent);
    lv_obj_set_size(button, width, 48);
    lv_obj_set_pos(button, x, y);
    UiComponents::styleSettingsCard(button, lv_color_hex(0x10242B), 18,
                                    lv_color_hex(0x62E8CA), LV_OPA_90);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    lv_obj_t * label = makeLabel(button, text, lv_color_hex(0xEFFFFB), nullptr);
    lv_obj_center(label);
    if(label_out) *label_out = label;
    return button;
}

void formatCountdown(uint32_t remaining_ms, char * out, size_t out_size) {
    const uint32_t seconds = (remaining_ms + 999UL) / 1000UL;
    snprintf(out, out_size, "%02lu:%02lu",
             static_cast<unsigned long>(seconds / 60UL),
             static_cast<unsigned long>(seconds % 60UL));
}

void formatStopwatch(int64_t elapsed_us, char * out, size_t out_size) {
    const uint64_t centiseconds = elapsed_us > 0
        ? static_cast<uint64_t>(elapsed_us) / 10000ULL
        : 0;
    snprintf(out, out_size, "%02llu:%02llu.%02llu",
             static_cast<unsigned long long>(centiseconds / 6000ULL),
             static_cast<unsigned long long>((centiseconds / 100ULL) % 60ULL),
             static_cast<unsigned long long>(centiseconds % 100ULL));
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
    remaining_ms_ = duration_ms;
    expiry_consumed_ = false;
}

void CountdownTimer::pause(uint32_t now_ms) {
    if(!running_) return;
    remaining_ms_ = remainingMs(now_ms);
    running_ = false;
    target_ms_ = 0;
}

void CountdownTimer::resume(uint32_t now_ms) {
    if(running_ || remaining_ms_ == 0) return;
    target_ms_ = now_ms + remaining_ms_;
    running_ = true;
    expiry_consumed_ = false;
}

void CountdownTimer::reset(uint32_t duration_ms) {
    running_ = false;
    target_ms_ = 0;
    remaining_ms_ = duration_ms;
    expiry_consumed_ = false;
}

void CountdownTimer::stop() {
    running_ = false;
    target_ms_ = 0;
    remaining_ms_ = 0;
    expiry_consumed_ = false;
}

uint32_t CountdownTimer::remainingMs(uint32_t now_ms) const {
    if(!running_) {
        return remaining_ms_;
    }
    if(static_cast<int32_t>(target_ms_ - now_ms) <= 0) {
        return 0;
    }
    return target_ms_ - now_ms;
}

bool CountdownTimer::expired(uint32_t now_ms) const {
    return running_ && remainingMs(now_ms) == 0;
}

bool CountdownTimer::consumeExpired(uint32_t now_ms) {
    if(expiry_consumed_ || !expired(now_ms)) return false;
    expiry_consumed_ = true;
    running_ = false;
    target_ms_ = 0;
    remaining_ms_ = 0;
    return true;
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

    overview_page_ = makePage(root_);
    timer_page_ = makePage(root_);
    stopwatch_page_ = makePage(root_);
    lv_obj_add_flag(timer_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(stopwatch_page_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * title = makeLabel(overview_page_, "Clock", lv_color_hex(0xEFFFFB), &lv_font_montserrat_24);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 30, 58);

    time_label_ = makeLabel(overview_page_, "--:--", lv_color_hex(0xEFFFFB), &lv_font_montserrat_48);
    lv_obj_align(time_label_, LV_ALIGN_TOP_LEFT, 30, 104);

    date_label_ = makeLabel(overview_page_, "RTC not ready", lv_color_hex(0x8BA6AA), nullptr);
    lv_obj_align(date_label_, LV_ALIGN_TOP_LEFT, 34, 170);

    lv_obj_t * next_card = makeCard(overview_page_, 28, 210, 354, 72, lv_color_hex(0x62E8CA));
    next_alarm_label_ = makeLabel(next_card, "Next alarm  --:--", lv_color_hex(0xEFFFFB), nullptr);
    lv_obj_align(next_alarm_label_, LV_ALIGN_LEFT_MID, 0, 0);

    for(uint8_t slot = 0; slot < AlarmService::kSlots; ++slot) {
        lv_obj_t * card = makeCard(overview_page_, 28, 296 + static_cast<lv_coord_t>(slot) * 68,
                                   170, 56, lv_color_hex(0x72CDE0));
        alarm_time_labels_[slot] = makeLabel(card, "--:--", lv_color_hex(0xEFFFFB), &lv_font_montserrat_24);
        lv_obj_align(alarm_time_labels_[slot], LV_ALIGN_LEFT_MID, 0, -7);
        alarm_detail_labels_[slot] = makeLabel(card, "Empty", lv_color_hex(0x8BA6AA), nullptr);
        lv_obj_align(alarm_detail_labels_[slot], LV_ALIGN_LEFT_MID, 0, 16);
    }

    lv_obj_t * timer_card = makeCard(overview_page_, 212, 296, 170, 56, lv_color_hex(0xB0FF63));
    timer_label_ = makeLabel(timer_card, "Timer 10:00", lv_color_hex(0xEFFFFB), nullptr);
    lv_obj_center(timer_label_);
    lv_obj_add_flag(timer_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(timer_card, openTimerEvent, LV_EVENT_CLICKED, this);

    lv_obj_t * stopwatch_card = makeCard(overview_page_, 212, 364, 170, 56, lv_color_hex(0xFF9148));
    stopwatch_label_ = makeLabel(stopwatch_card, "Stopwatch 00:00", lv_color_hex(0xEFFFFB), nullptr);
    lv_obj_center(stopwatch_label_);
    lv_obj_add_flag(stopwatch_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(stopwatch_card, openStopwatchEvent, LV_EVENT_CLICKED, this);

    makeButton(timer_page_, 24, 48, 48, LV_SYMBOL_LEFT,
               backToOverviewEvent, this);
    lv_obj_t * timer_title = makeLabel(timer_page_, "Timer",
                                      lv_color_hex(0xEFFFFB),
                                      &lv_font_montserrat_24);
    lv_obj_set_pos(timer_title, 92, 58);
    timer_value_label_ = makeLabel(timer_page_, "10:00",
                                  lv_color_hex(0xEFFFFB),
                                  &lv_font_montserrat_48);
    lv_obj_align(timer_value_label_, LV_ALIGN_TOP_MID, 0, 132);
    static const uint32_t durations[] = {60000UL, 300000UL, 600000UL};
    static const char * duration_labels[] = {"1 min", "5 min", "10 min"};
    for(uint8_t i = 0; i < 3; ++i) {
        timer_presets_[i].app = this;
        timer_presets_[i].duration_ms = durations[i];
        makeButton(timer_page_, 28 + static_cast<lv_coord_t>(i) * 121,
                   244, 112, duration_labels[i], timerPresetEvent,
                   &timer_presets_[i]);
    }
    makeButton(timer_page_, 28, 350, 170, "Start", timerToggleEvent,
               this, &timer_toggle_label_);
    makeButton(timer_page_, 212, 350, 170, "Reset", timerResetEvent, this);

    makeButton(stopwatch_page_, 24, 48, 48, LV_SYMBOL_LEFT,
               backToOverviewEvent, this);
    lv_obj_t * stopwatch_title = makeLabel(stopwatch_page_, "Stopwatch",
                                          lv_color_hex(0xEFFFFB),
                                          &lv_font_montserrat_24);
    lv_obj_set_pos(stopwatch_title, 92, 58);
    stopwatch_value_label_ = makeLabel(stopwatch_page_, "00:00.00",
                                      lv_color_hex(0xEFFFFB),
                                      &lv_font_montserrat_48);
    lv_obj_align(stopwatch_value_label_, LV_ALIGN_TOP_MID, 0, 154);
    makeButton(stopwatch_page_, 28, 350, 170, "Start",
               stopwatchToggleEvent, this, &stopwatch_toggle_label_);
    makeButton(stopwatch_page_, 212, 350, 170, "Reset",
               stopwatchResetEvent, this);

    timer_.reset(selected_timer_ms_);
    refreshSessionLabels(lv_tick_get(), esp_timer_get_time());
    hide();
    return true;
}

void ClockApp::openTimerEvent(lv_event_t * event) {
    ClockApp * app = static_cast<ClockApp *>(lv_event_get_user_data(event));
    if(app) app->showSessionPage(app->timer_page_);
}

void ClockApp::openStopwatchEvent(lv_event_t * event) {
    ClockApp * app = static_cast<ClockApp *>(lv_event_get_user_data(event));
    if(app) app->showSessionPage(app->stopwatch_page_);
}

void ClockApp::backToOverviewEvent(lv_event_t * event) {
    ClockApp * app = static_cast<ClockApp *>(lv_event_get_user_data(event));
    if(app) app->showSessionPage(nullptr);
}

void ClockApp::timerPresetEvent(lv_event_t * event) {
    TimerPresetContext * preset =
        static_cast<TimerPresetContext *>(lv_event_get_user_data(event));
    if(!preset || !preset->app || preset->duration_ms == 0) return;
    preset->app->selected_timer_ms_ = preset->duration_ms;
    preset->app->timer_.reset(preset->duration_ms);
    preset->app->refreshSessionLabels(lv_tick_get(), esp_timer_get_time());
}

void ClockApp::timerToggleEvent(lv_event_t * event) {
    ClockApp * app = static_cast<ClockApp *>(lv_event_get_user_data(event));
    if(!app) return;
    const uint32_t now_ms = lv_tick_get();
    if(app->timer_.running()) {
        app->timer_.pause(now_ms);
    } else {
        if(app->timer_.remainingMs(now_ms) == 0) {
            app->timer_.reset(app->selected_timer_ms_);
        }
        app->timer_.resume(now_ms);
    }
    app->refreshSessionLabels(now_ms, esp_timer_get_time());
}

void ClockApp::timerResetEvent(lv_event_t * event) {
    ClockApp * app = static_cast<ClockApp *>(lv_event_get_user_data(event));
    if(!app) return;
    app->timer_.reset(app->selected_timer_ms_);
    app->refreshSessionLabels(lv_tick_get(), esp_timer_get_time());
}

void ClockApp::stopwatchToggleEvent(lv_event_t * event) {
    ClockApp * app = static_cast<ClockApp *>(lv_event_get_user_data(event));
    if(!app) return;
    const int64_t now_us = esp_timer_get_time();
    if(app->stopwatch_.running()) app->stopwatch_.pause(now_us);
    else app->stopwatch_.start(now_us);
    app->refreshSessionLabels(lv_tick_get(), now_us);
}

void ClockApp::stopwatchResetEvent(lv_event_t * event) {
    ClockApp * app = static_cast<ClockApp *>(lv_event_get_user_data(event));
    if(!app) return;
    app->stopwatch_.reset();
    app->refreshSessionLabels(lv_tick_get(), esp_timer_get_time());
}

void ClockApp::showSessionPage(lv_obj_t * page) {
    if(!overview_page_ || !timer_page_ || !stopwatch_page_) return;
    lv_obj_add_flag(timer_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(stopwatch_page_, LV_OBJ_FLAG_HIDDEN);
    if(page) {
        lv_obj_add_flag(overview_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(page);
    } else {
        lv_obj_clear_flag(overview_page_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(overview_page_);
    }
}

void ClockApp::refreshSessionLabels(uint32_t now_ms, int64_t now_us) {
    char timer_text[16];
    formatCountdown(timer_.remainingMs(now_ms), timer_text,
                    sizeof(timer_text));
    setLabel(timer_value_label_, timer_text);
    char overview_timer[24];
    snprintf(overview_timer, sizeof(overview_timer), "Timer %s", timer_text);
    setLabel(timer_label_, overview_timer);
    setLabel(timer_toggle_label_, timer_.running() ? "Pause" : "Start");

    char stopwatch_text[20];
    formatStopwatch(stopwatch_.elapsedUs(now_us), stopwatch_text,
                    sizeof(stopwatch_text));
    setLabel(stopwatch_value_label_, stopwatch_text);
    char overview_stopwatch[32];
    snprintf(overview_stopwatch, sizeof(overview_stopwatch), "Stopwatch %s",
             stopwatch_text);
    setLabel(stopwatch_label_, overview_stopwatch);
    setLabel(stopwatch_toggle_label_, stopwatch_.running() ? "Pause" : "Start");
}

void ClockApp::tick(uint32_t now_ms, int64_t now_us) {
    refreshSessionLabels(now_ms, now_us);
}

bool ClockApp::consumeTimerExpired(uint32_t now_ms) {
    const bool expired = timer_.consumeExpired(now_ms);
    if(expired) refreshSessionLabels(now_ms, esp_timer_get_time());
    return expired;
}

void ClockApp::destroy() {
    if(root_) {
        lv_obj_del(root_);
    }
    root_ = nullptr;
    overview_page_ = nullptr;
    timer_page_ = nullptr;
    stopwatch_page_ = nullptr;
    time_label_ = nullptr;
    date_label_ = nullptr;
    next_alarm_label_ = nullptr;
    for(uint8_t slot = 0; slot < AlarmService::kSlots; ++slot) {
        alarm_time_labels_[slot] = nullptr;
        alarm_detail_labels_[slot] = nullptr;
    }
    timer_label_ = nullptr;
    stopwatch_label_ = nullptr;
    timer_value_label_ = nullptr;
    timer_toggle_label_ = nullptr;
    stopwatch_value_label_ = nullptr;
    stopwatch_toggle_label_ = nullptr;
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
