#pragma once

#include <stdint.h>
#include <lvgl.h>

#include "../../core/SystemState.h"
#include "../../services/AlarmService.h"
#include "../../services/TimeService.h"
#include "../../ui/UiComponents.h"

namespace firefly {

class CountdownTimer {
public:
    void start(uint32_t duration_ms, uint32_t now_ms);
    void pause(uint32_t now_ms);
    void resume(uint32_t now_ms);
    void reset(uint32_t duration_ms);
    void stop();
    bool running() const { return running_; }
    uint32_t remainingMs(uint32_t now_ms) const;
    bool expired(uint32_t now_ms) const;
    bool consumeExpired(uint32_t now_ms);

private:
    bool running_ = false;
    uint32_t target_ms_ = 0;
    uint32_t remaining_ms_ = 0;
    bool expiry_consumed_ = false;
};

class StopwatchSession {
public:
    void start(int64_t now_us);
    void pause(int64_t now_us);
    void reset();
    bool running() const { return running_; }
    int64_t elapsedUs(int64_t now_us) const;

private:
    bool running_ = false;
    int64_t started_at_us_ = 0;
    int64_t accumulated_us_ = 0;
};

class ClockApp {
public:
    bool create(lv_obj_t * parent,
                UiComponents & components,
                TimeService & time,
                AlarmService & alarms);
    void destroy();
    void show();
    void hide();
    void refresh(const SystemState & state);
    void tick(uint32_t now_ms, int64_t now_us);
    bool timerExpired(uint32_t now_ms) const { return timer_.expired(now_ms); }
    bool consumeTimerExpired(uint32_t now_ms);
    lv_obj_t * root() const { return root_; }

private:
    struct TimerPresetContext {
        ClockApp * app = nullptr;
        uint32_t duration_ms = 0;
    };

    static void openTimerEvent(lv_event_t * event);
    static void openStopwatchEvent(lv_event_t * event);
    static void backToOverviewEvent(lv_event_t * event);
    static void timerPresetEvent(lv_event_t * event);
    static void timerToggleEvent(lv_event_t * event);
    static void timerResetEvent(lv_event_t * event);
    static void stopwatchToggleEvent(lv_event_t * event);
    static void stopwatchResetEvent(lv_event_t * event);
    void showSessionPage(lv_obj_t * page);
    void refreshSessionLabels(uint32_t now_ms, int64_t now_us);
    void refreshAlarmRows(int64_t now_epoch);

    lv_obj_t * root_ = nullptr;
    lv_obj_t * overview_page_ = nullptr;
    lv_obj_t * timer_page_ = nullptr;
    lv_obj_t * stopwatch_page_ = nullptr;
    lv_obj_t * time_label_ = nullptr;
    lv_obj_t * date_label_ = nullptr;
    lv_obj_t * next_alarm_label_ = nullptr;
    lv_obj_t * alarm_time_labels_[AlarmService::kSlots]{};
    lv_obj_t * alarm_detail_labels_[AlarmService::kSlots]{};
    lv_obj_t * timer_label_ = nullptr;
    lv_obj_t * stopwatch_label_ = nullptr;
    lv_obj_t * timer_value_label_ = nullptr;
    lv_obj_t * timer_toggle_label_ = nullptr;
    lv_obj_t * stopwatch_value_label_ = nullptr;
    lv_obj_t * stopwatch_toggle_label_ = nullptr;
    TimeService * time_ = nullptr;
    AlarmService * alarms_ = nullptr;
    CountdownTimer timer_{};
    StopwatchSession stopwatch_{};
    uint32_t selected_timer_ms_ = 10UL * 60UL * 1000UL;
    TimerPresetContext timer_presets_[3]{};
};

}  // namespace firefly
