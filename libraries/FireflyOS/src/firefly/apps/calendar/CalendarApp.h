#pragma once

#include <stdint.h>
#include <lvgl.h>

#include "../../core/SystemState.h"
#include "../../ui/UiComponents.h"

namespace firefly {

struct CalendarDay {
    uint8_t day = 0;
    bool in_current_month = false;
    bool today = false;
};

struct CalendarMonth {
    int16_t year = 1970;
    uint8_t month = 1;
    uint8_t first_weekday = 0;
    uint8_t days_in_month = 0;
    CalendarDay cells[42]{};
};

class CalendarModel {
public:
    static CalendarMonth buildMonth(int16_t year, uint8_t month, uint8_t today_day = 0);
    static CalendarMonth shiftMonth(int16_t year, uint8_t month, int8_t delta_months);

private:
    static uint8_t daysInMonth(int16_t year, uint8_t month);
    static uint8_t firstWeekday(int16_t year, uint8_t month);
};

struct CalendarSummary {
    bool valid = false;
    int64_t start_epoch = 0;
    char title[32]{};
};

class CalendarAgendaCache {
public:
    static constexpr uint8_t kMaxSummaries = 8;

    void clear();
    void setSummaries(const CalendarSummary * summaries,
                      uint8_t count,
                      int64_t last_updated_epoch);
    uint8_t count() const { return count_; }
    int64_t lastUpdatedEpoch() const { return last_updated_epoch_; }
    const CalendarSummary & at(uint8_t index) const;

private:
    CalendarSummary summaries_[kMaxSummaries]{};
    CalendarSummary empty_{};
    uint8_t count_ = 0;
    int64_t last_updated_epoch_ = 0;
};

class CalendarApp {
public:
    bool create(lv_obj_t * parent, UiComponents & components);
    void destroy();
    void show();
    void hide();
    void refresh(const SystemState & state);
    void setMonth(const CalendarMonth & month);
    void setAgenda(const CalendarAgendaCache & agenda);
    lv_obj_t * root() const { return root_; }

private:
    static void previousMonthEvent(lv_event_t * event);
    static void nextMonthEvent(lv_event_t * event);
    void shiftDisplayedMonth(int8_t delta_months);
    void refreshMonthLabels();
    void refreshAgendaLabels();

    lv_obj_t * root_ = nullptr;
    lv_obj_t * title_label_ = nullptr;
    lv_obj_t * sync_label_ = nullptr;
    lv_obj_t * day_labels_[42]{};
    lv_obj_t * agenda_labels_[CalendarAgendaCache::kMaxSummaries]{};
    CalendarMonth month_{};
    CalendarAgendaCache agenda_{};
    bool browsing_month_ = false;
};

}  // namespace firefly
