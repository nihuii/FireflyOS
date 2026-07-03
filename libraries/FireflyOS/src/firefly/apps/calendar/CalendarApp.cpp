#include "CalendarApp.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../ui/UiTheme.h"

namespace firefly {
namespace {

void normalizeMonth(int16_t & year, int16_t & month) {
    while(month < 1) {
        month += 12;
        --year;
    }
    while(month > 12) {
        month -= 12;
        ++year;
    }
}

uint8_t clampMonth(uint8_t month) {
    if(month < 1) return 1;
    if(month > 12) return 12;
    return month;
}

const char * weekdayName(uint8_t index) {
    static const char * names[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    return names[index < 7 ? index : 0];
}

}  // namespace

uint8_t CalendarModel::daysInMonth(int16_t year, uint8_t month) {
    month = clampMonth(month);
    struct tm last_day{};
    last_day.tm_year = year - 1900;
    last_day.tm_mon = month;
    last_day.tm_mday = 0;
    last_day.tm_hour = 12;
    last_day.tm_isdst = -1;
    const time_t epoch = mktime(&last_day);
    if(epoch < 0) {
        static const uint8_t fallback[] = {31, 28, 31, 30, 31, 30,
                                           31, 31, 30, 31, 30, 31};
        if(month == 2 &&
           ((year % 400 == 0) || (year % 4 == 0 && year % 100 != 0))) {
            return 29;
        }
        return fallback[month - 1];
    }
    return static_cast<uint8_t>(last_day.tm_mday);
}

uint8_t CalendarModel::firstWeekday(int16_t year, uint8_t month) {
    month = clampMonth(month);
    struct tm first_day{};
    first_day.tm_year = year - 1900;
    first_day.tm_mon = month - 1;
    first_day.tm_mday = 1;
    first_day.tm_hour = 12;
    first_day.tm_isdst = -1;
    const time_t epoch = mktime(&first_day);
    if(epoch < 0) return 0;
    return static_cast<uint8_t>(first_day.tm_wday);
}

CalendarMonth CalendarModel::buildMonth(int16_t year,
                                        uint8_t month,
                                        uint8_t today_day) {
    month = clampMonth(month);
    CalendarMonth result{};
    result.year = year;
    result.month = month;
    result.first_weekday = firstWeekday(year, month);
    result.days_in_month = daysInMonth(year, month);

    int16_t previous_year = year;
    int16_t previous_month = static_cast<int16_t>(month) - 1;
    normalizeMonth(previous_year, previous_month);
    const uint8_t previous_days =
        daysInMonth(previous_year, static_cast<uint8_t>(previous_month));

    for(uint8_t index = 0; index < 42; ++index) {
        CalendarDay & cell = result.cells[index];
        if(index < result.first_weekday) {
            cell.day = static_cast<uint8_t>(
                previous_days - (result.first_weekday - index) + 1U);
            cell.in_current_month = false;
            cell.today = false;
        } else {
            const uint8_t current_day =
                static_cast<uint8_t>(index - result.first_weekday + 1U);
            if(current_day <= result.days_in_month) {
                cell.day = current_day;
                cell.in_current_month = true;
                cell.today = today_day > 0 && current_day == today_day;
            } else {
                cell.day = static_cast<uint8_t>(current_day - result.days_in_month);
                cell.in_current_month = false;
                cell.today = false;
            }
        }
    }
    return result;
}

CalendarMonth CalendarModel::shiftMonth(int16_t year,
                                        uint8_t month,
                                        int8_t delta_months) {
    int16_t normalized_month = static_cast<int16_t>(month) + delta_months;
    normalizeMonth(year, normalized_month);
    return buildMonth(year, static_cast<uint8_t>(normalized_month));
}

void CalendarAgendaCache::clear() {
    for(uint8_t i = 0; i < kMaxSummaries; ++i) {
        summaries_[i] = CalendarSummary{};
    }
    count_ = 0;
    last_updated_epoch_ = 0;
}

void CalendarAgendaCache::setSummaries(const CalendarSummary * summaries,
                                       uint8_t count,
                                       int64_t last_updated_epoch) {
    clear();
    if(!summaries) return;
    count_ = count > kMaxSummaries ? kMaxSummaries : count;
    for(uint8_t i = 0; i < count_; ++i) {
        summaries_[i] = summaries[i];
        summaries_[i].title[sizeof(summaries_[i].title) - 1] = '\0';
    }
    last_updated_epoch_ = last_updated_epoch;
}

const CalendarSummary & CalendarAgendaCache::at(uint8_t index) const {
    if(index >= count_) return empty_;
    return summaries_[index];
}

bool CalendarApp::create(lv_obj_t * parent, UiComponents & components) {
    LV_UNUSED(components);
    if(!parent) return false;

    const UiTokens tokens = UiTheme::fireflyDefault();
    root_ = UiComponents::createPage(parent, tokens);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    title_label_ = lv_label_create(root_);
    lv_obj_set_width(title_label_, 220);
    lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title_label_, lv_color_hex(tokens.text_primary), 0);
    lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_24, 0);
    lv_label_set_text(title_label_, "Calendar");
    lv_obj_align(title_label_, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t * previous = lv_btn_create(root_);
    lv_obj_set_size(previous, 48, 48);
    lv_obj_set_pos(previous, 24, 48);
    lv_obj_set_style_radius(previous, 18, 0);
    lv_obj_set_style_bg_color(previous, lv_color_hex(tokens.bg_surface), 0);
    lv_obj_add_event_cb(previous, previousMonthEvent, LV_EVENT_CLICKED, this);
    lv_obj_t * previous_label = lv_label_create(previous);
    lv_label_set_text(previous_label, LV_SYMBOL_LEFT);
    lv_obj_center(previous_label);

    lv_obj_t * next = lv_btn_create(root_);
    lv_obj_set_size(next, 48, 48);
    lv_obj_set_pos(next, 338, 48);
    lv_obj_set_style_radius(next, 18, 0);
    lv_obj_set_style_bg_color(next, lv_color_hex(tokens.bg_surface), 0);
    lv_obj_add_event_cb(next, nextMonthEvent, LV_EVENT_CLICKED, this);
    lv_obj_t * next_label = lv_label_create(next);
    lv_label_set_text(next_label, LV_SYMBOL_RIGHT);
    lv_obj_center(next_label);

    sync_label_ = lv_label_create(root_);
    lv_obj_set_width(sync_label_, 250);
    lv_obj_set_style_text_align(sync_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(sync_label_, lv_color_hex(tokens.text_secondary), 0);
    lv_label_set_text(sync_label_, "Offline month | Phone not linked");
    lv_obj_set_pos(sync_label_, 80, 102);

    static const uint8_t left = 28;
    static const uint8_t top = 142;
    static const uint8_t cell_w = 50;
    for(uint8_t column = 0; column < 7; ++column) {
        lv_obj_t * weekday = lv_label_create(root_);
        lv_obj_set_width(weekday, 42);
        lv_obj_set_style_text_align(weekday, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(weekday, lv_color_hex(tokens.firefly_primary), 0);
        lv_label_set_text(weekday, weekdayName(column));
        lv_obj_set_pos(weekday, left + column * cell_w, 124);
    }

    for(uint8_t index = 0; index < 42; ++index) {
        day_labels_[index] = lv_label_create(root_);
        lv_obj_set_width(day_labels_[index], 42);
        lv_obj_set_style_text_align(day_labels_[index], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(day_labels_[index],
                       left + (index % 7U) * cell_w,
                       top + (index / 7U) * 28U);
    }

    for(uint8_t i = 0; i < CalendarAgendaCache::kMaxSummaries; ++i) {
        agenda_labels_[i] = lv_label_create(root_);
        lv_obj_set_width(agenda_labels_[i], 330);
        lv_obj_set_style_text_color(agenda_labels_[i],
                                    lv_color_hex(tokens.text_secondary), 0);
        lv_obj_set_pos(agenda_labels_[i], 34, 314 + i * 18);
    }

    month_ = CalendarModel::buildMonth(2026, 7, 2);
    refreshMonthLabels();
    refreshAgendaLabels();
    return true;
}

void CalendarApp::destroy() {
    if(root_) {
        lv_obj_del(root_);
        root_ = nullptr;
    }
}

void CalendarApp::show() {
    if(root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void CalendarApp::hide() {
    if(root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void CalendarApp::refresh(const SystemState & state) {
    if(state.time.valid && !browsing_month_) {
        const time_t raw = static_cast<time_t>(state.time.epoch_seconds);
        struct tm local{};
        if(localtime_r(&raw, &local)) {
            month_ = CalendarModel::buildMonth(
                static_cast<int16_t>(local.tm_year + 1900),
                static_cast<uint8_t>(local.tm_mon + 1),
                static_cast<uint8_t>(local.tm_mday));
        }
    }
    refreshMonthLabels();
    refreshAgendaLabels();
}

void CalendarApp::setMonth(const CalendarMonth & month) {
    month_ = month;
    browsing_month_ = true;
    refreshMonthLabels();
}

void CalendarApp::setAgenda(const CalendarAgendaCache & agenda) {
    agenda_ = agenda;
    refreshAgendaLabels();
}

void CalendarApp::previousMonthEvent(lv_event_t * event) {
    CalendarApp * app = static_cast<CalendarApp *>(lv_event_get_user_data(event));
    if(app) app->shiftDisplayedMonth(-1);
}

void CalendarApp::nextMonthEvent(lv_event_t * event) {
    CalendarApp * app = static_cast<CalendarApp *>(lv_event_get_user_data(event));
    if(app) app->shiftDisplayedMonth(1);
}

void CalendarApp::shiftDisplayedMonth(int8_t delta_months) {
    month_ = CalendarModel::shiftMonth(month_.year, month_.month, delta_months);
    browsing_month_ = true;
    refreshMonthLabels();
}

void CalendarApp::refreshMonthLabels() {
    if(!root_) return;
    char title[32];
    snprintf(title, sizeof(title), "%04d/%02u",
             static_cast<int>(month_.year),
             static_cast<unsigned>(month_.month));
    lv_label_set_text(title_label_, title);

    for(uint8_t i = 0; i < 42; ++i) {
        char day_text[4];
        snprintf(day_text, sizeof(day_text), "%u",
                 static_cast<unsigned>(month_.cells[i].day));
        lv_label_set_text(day_labels_[i], day_text);
        const uint32_t color = month_.cells[i].today
            ? 0x62E8CA
            : (month_.cells[i].in_current_month ? 0xEFFFFB : 0x536B70);
        lv_obj_set_style_text_color(day_labels_[i], lv_color_hex(color), 0);
    }
}

void CalendarApp::refreshAgendaLabels() {
    if(!root_) return;
    char sync[64];
    snprintf(sync, sizeof(sync), "8 max | Last sync %lld",
             static_cast<long long>(agenda_.lastUpdatedEpoch()));
    lv_label_set_text(sync_label_, agenda_.count() > 0
        ? sync
        : "Offline month | Phone not linked");

    for(uint8_t i = 0; i < CalendarAgendaCache::kMaxSummaries; ++i) {
        if(i < agenda_.count() && agenda_.at(i).valid) {
            char line[48];
            snprintf(line, sizeof(line), "%u. %s",
                     static_cast<unsigned>(i + 1U), agenda_.at(i).title);
            lv_label_set_text(agenda_labels_[i], line);
        } else {
            lv_label_set_text(agenda_labels_[i], i == 0 ? "No synced events" : "");
        }
    }
}

}  // namespace firefly
