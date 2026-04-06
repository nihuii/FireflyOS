#include "FireflyApp.h"

#include <sys/time.h>

namespace {

uint8_t sleep_icon_index = 0;
bool sleep_icon_has_been_shown = false;

String two_digit_text(uint8_t value) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02u", value);
    return String(buf);
}

String alarm_time_text() {
    return two_digit_text(alarm_hour) + ":" + two_digit_text(alarm_minute);
}

const char * battery_symbol_for_percent(int percent) {
    if(percent >= 85) return LV_SYMBOL_BATTERY_FULL;
    if(percent >= 60) return LV_SYMBOL_BATTERY_3;
    if(percent >= 35) return LV_SYMBOL_BATTERY_2;
    if(percent >= 15) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

lv_color_t battery_color_for_percent(int percent) {
    if(percent >= 60) return lv_color_hex(0x74F7A3);
    if(percent >= 30) return lv_color_hex(0xFFD166);
    return lv_color_hex(0xFF7A7A);
}

String brightness_percent_text() {
    return String((screen_brightness * 100U) / 255U) + "%";
}

void refresh_control_center_ui_impl() {
    const int battery_percent = power.getBatteryPercent();

    if(notif_detail_label) {
        String detail = "Battery " + String(battery_percent) + "%";
        if(power.isCharging()) {
            detail += "  Charging";
        }
        detail += "\nVolume " + String(volume_level) + "%  Brightness " + brightness_percent_text();
        lv_label_set_text(notif_detail_label, detail.c_str());
    }

    if(notif_volume_slider) {
        lv_slider_set_value(notif_volume_slider, volume_level, LV_ANIM_OFF);
    }
    if(notif_volume_value_label) {
        lv_label_set_text(notif_volume_value_label, (String(volume_level) + "%").c_str());
    }
    if(notif_brightness_slider) {
        lv_slider_set_value(notif_brightness_slider, screen_brightness, LV_ANIM_OFF);
    }
    if(notif_brightness_value_label) {
        lv_label_set_text(notif_brightness_value_label, brightness_percent_text().c_str());
    }
    if(settings_brightness_slider) {
        lv_slider_set_value(settings_brightness_slider, screen_brightness, LV_ANIM_OFF);
    }
    if(settings_brightness_value_label) {
        lv_label_set_text(settings_brightness_value_label, brightness_percent_text().c_str());
    }
}

void refresh_battery_details_label() {
    if(!settings_batt_info) {
        return;
    }

    String info = "";
    info += "Battery " + String(power.getBatteryPercent()) + "%\n";
    info += "Temperature " + String(power.getTemperature()) + " C\n";
    info += "Charging " + String(power.isCharging() ? "YES" : "NO") + "\n";
    info += "VBUS " + String(power.isVbusIn() ? "YES" : "NO") + "\n";
    info += "Batt " + String(power.getBattVoltage()) + " mV\n";
    info += "System " + String(power.getSystemVoltage()) + " mV";
    lv_label_set_text(settings_batt_info, info.c_str());
}

void hide_charge_overlay() {
    charging_overlay_visible = false;
    if(charge_overlay) {
        lv_obj_add_flag(charge_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void show_charge_overlay() {
    charging_overlay_visible = true;
    charge_overlay_started_at = millis();

    if(charge_percent_label) {
        lv_label_set_text(charge_percent_label, (String(power.getBatteryPercent()) + "%").c_str());
    }
    if(charge_status_label) {
        lv_label_set_text(charge_status_label, power.isCharging() ? "Charging" : "Power Connected");
    }
    if(charge_overlay) {
        lv_obj_clear_flag(charge_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(charge_overlay);
    }
}

void trigger_alarm_alert(const String& current_time) {
    alarm_ringing = true;

    if(is_sleeping) {
        exit_sleep_screen_mode();
    }

    if(alarm_overlay_title) {
        lv_label_set_text(alarm_overlay_title, "Alarm");
    }
    if(alarm_overlay_detail) {
        String detail = "It is " + current_time + "\nVolume " + String(volume_level) + "%";
        lv_label_set_text(alarm_overlay_detail, detail.c_str());
    }
    if(alarm_overlay) {
        lv_obj_clear_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(alarm_overlay);
    }
}

void wake_sleep_screen_from_blackout() {
    if(!is_sleeping) {
        return;
    }

    sleep_display_off = false;
    sleep_entered_at = millis();
    gfx_co5300->setBrightness(screen_brightness);
    if(sleep_screen) {
        lv_obj_clear_flag(sleep_screen, LV_OBJ_FLAG_HIDDEN);
    }
}

void refresh_sleep_icon(bool advance) {
    if(!sleep_icon_img) {
        return;
    }

    if(advance && sleep_icon_has_been_shown) {
        sleep_icon_index = (sleep_icon_index + 1U) % SLEEP_ICON_COUNT;
    }

    lv_img_set_src(sleep_icon_img, get_sleep_icon_by_index(sleep_icon_index));
    sleep_icon_has_been_shown = true;
}

} // namespace

void sync_time_to_system_from_rtc(const RTC_DateTime& dt) {
    struct tm timeinfo = {0};
    timeinfo.tm_year = dt.getYear() - 1900;
    timeinfo.tm_mon = dt.getMonth() - 1;
    timeinfo.tm_mday = dt.getDay();
    timeinfo.tm_hour = dt.getHour();
    timeinfo.tm_min = dt.getMinute();
    timeinfo.tm_sec = dt.getSecond();

    const time_t t = mktime(&timeinfo);
    struct timeval now;
    now.tv_sec = t;
    now.tv_usec = 0;
    settimeofday(&now, NULL);
    setenv("TZ", "CST-8", 1);
    tzset();
}

void load_sound_alarm_preferences() {
    volume_level = prefs.getUChar(UI_PREF_VOLUME_KEY, 70);
    if(volume_level > 100) volume_level = 100;

    alarm_enabled = prefs.getBool(UI_PREF_ALARM_ENABLED_KEY, false);
    alarm_hour = prefs.getUChar(UI_PREF_ALARM_HOUR_KEY, 7);
    alarm_minute = prefs.getUChar(UI_PREF_ALARM_MINUTE_KEY, 30);
    if(alarm_hour > 23) alarm_hour = 7;
    if(alarm_minute > 59) alarm_minute = 30;
}

void save_volume_preference() {
    prefs.putUChar(UI_PREF_VOLUME_KEY, volume_level);
}

void save_alarm_preferences() {
    prefs.putBool(UI_PREF_ALARM_ENABLED_KEY, alarm_enabled);
    prefs.putUChar(UI_PREF_ALARM_HOUR_KEY, alarm_hour);
    prefs.putUChar(UI_PREF_ALARM_MINUTE_KEY, alarm_minute);
}

void open_settings_panel() {
    if(!settings_panel) {
        return;
    }
    set_settings_subpage(NULL);
    lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
}

void close_settings_panel() {
    if(!settings_panel) {
        return;
    }
    set_settings_subpage(NULL);
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
}

void set_settings_subpage(lv_obj_t * page) {
    if(settings_menu_container) lv_obj_add_flag(settings_menu_container, LV_OBJ_FLAG_HIDDEN);
    if(settings_batt_container) lv_obj_add_flag(settings_batt_container, LV_OBJ_FLAG_HIDDEN);
    if(settings_time_container) lv_obj_add_flag(settings_time_container, LV_OBJ_FLAG_HIDDEN);
    if(settings_sound_container) lv_obj_add_flag(settings_sound_container, LV_OBJ_FLAG_HIDDEN);
    if(settings_display_container) lv_obj_add_flag(settings_display_container, LV_OBJ_FLAG_HIDDEN);

    if(page) {
        lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    } else if(settings_menu_container) {
        lv_obj_clear_flag(settings_menu_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void refresh_runtime_status_ui() {
    refresh_control_center_ui_impl();
}

void refresh_battery_ui() {
    const int battery_percent = power.getBatteryPercent();
    const char * battery_symbol = battery_symbol_for_percent(battery_percent);
    const lv_color_t battery_color = battery_color_for_percent(battery_percent);

    if(status_battery_icon) {
        lv_label_set_text(status_battery_icon, battery_symbol);
        lv_obj_set_style_text_color(status_battery_icon, battery_color, 0);
    }
    if(settings_batt_icon) {
        lv_label_set_text(settings_batt_icon, battery_symbol);
        lv_obj_set_style_text_color(settings_batt_icon, battery_color, 0);
    }

    refresh_battery_details_label();
    refresh_runtime_status_ui();
}

void refresh_sound_alarm_ui() {
    if(settings_volume_slider) {
        lv_slider_set_value(settings_volume_slider, volume_level, LV_ANIM_OFF);
    }
    if(settings_volume_value_label) {
        lv_label_set_text(settings_volume_value_label, (String(volume_level) + "%").c_str());
    }
    if(settings_alarm_status_label) {
        lv_label_set_text(settings_alarm_status_label, alarm_enabled ? "Alarm enabled" : "Alarm disabled");
    }
    if(settings_alarm_time_label) {
        lv_label_set_text(settings_alarm_time_label, ("Time " + alarm_time_text()).c_str());
    }
    if(settings_alarm_switch) {
        if(alarm_enabled) {
            lv_obj_add_state(settings_alarm_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(settings_alarm_switch, LV_STATE_CHECKED);
        }
    }
    if(roller_alarm_hour) {
        lv_roller_set_selected(roller_alarm_hour, alarm_hour, LV_ANIM_OFF);
    }
    if(roller_alarm_minute) {
        lv_roller_set_selected(roller_alarm_minute, alarm_minute, LV_ANIM_OFF);
    }

    refresh_runtime_status_ui();
}

void set_screen_brightness_level(uint8_t brightness) {
    if(brightness < 20) brightness = 20;
    if(brightness > 255) brightness = 255;
    screen_brightness = brightness;
    if(!sleep_display_off) {
        gfx_co5300->setBrightness(screen_brightness);
    }
    refresh_runtime_status_ui();
}

void dismiss_alarm_alert() {
    alarm_ringing = false;
    if(alarm_overlay) {
        lv_obj_add_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void update_charging_overlay() {
    static unsigned long last_charge_poll_at = 0;
    const unsigned long now = millis();

    if(charging_overlay_visible && now - charge_overlay_started_at > 2800UL) {
        hide_charge_overlay();
    }

    const unsigned long poll_interval = charging_overlay_visible ? 120UL : 350UL;
    if(now - last_charge_poll_at < poll_interval) {
        return;
    }
    last_charge_poll_at = now;

    const bool charging_now = power.isCharging() || power.isVbusIn();
    if(charging_now && !charging_last_state) {
        show_charge_overlay();
    } else if(!charging_now && charging_last_state) {
        hide_charge_overlay();
    }
    charging_last_state = charging_now;

    if(!charging_overlay_visible) {
        return;
    }

    if(charge_percent_label) {
        lv_label_set_text(charge_percent_label, (String(power.getBatteryPercent()) + "%").c_str());
    }
    if(charge_status_label) {
        lv_label_set_text(charge_status_label, charging_now ? "Charging" : "Power Connected");
    }
}

void tv_event_cb(lv_event_t * e) {
    lv_obj_t * tv = lv_event_get_target(e);
    lv_obj_t * active = lv_tileview_get_tile_act(tv);

    if(active == tile_sys) {
        is_on_lockscreen = false;
        if(top_status_bar) {
            lv_obj_clear_flag(top_status_bar, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        is_on_lockscreen = true;
        if(top_status_bar) {
            lv_obj_add_flag(top_status_bar, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void anim_notif_panel_cb(void * var, int32_t v) {
    LV_UNUSED(var);
    if(!notif_panel) {
        return;
    }

    lv_obj_set_y(notif_panel, v);

    const int32_t dy = v + 502;
    const int32_t percent = (dy * 256) / 502;
    int32_t small_opa = 255 - percent;
    int32_t large_opa = percent;

    if(small_opa < 0) small_opa = 0;
    if(small_opa > 255) small_opa = 255;
    if(large_opa < 0) large_opa = 0;
    if(large_opa > 255) large_opa = 255;

    if(status_battery_icon) {
        lv_obj_set_style_text_opa(status_battery_icon, small_opa, 0);
    }
    if(status_time_label) {
        lv_obj_set_style_text_opa(status_time_label, small_opa, 0);
    }
    if(notif_time_label) {
        lv_obj_set_style_text_opa(notif_time_label, large_opa, 0);
        lv_obj_align(notif_time_label, LV_ALIGN_TOP_LEFT, 50 + (10 * percent / 256), 20 + (10 * percent / 256));
    }
}

void status_drag_cb(lv_event_t * e) {
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t * indev = lv_indev_get_act();
    if(!indev || !notif_panel) {
        return;
    }

    if(code == LV_EVENT_PRESSED) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        drag_start_y = point.y - lv_obj_get_y(notif_panel);
        is_dragging_notif = true;
    } else if(code == LV_EVENT_PRESSING && is_dragging_notif) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        lv_coord_t new_y = point.y - drag_start_y;
        if(new_y < -502) new_y = -502;
        if(new_y > 0) new_y = 0;
        anim_notif_panel_cb(notif_panel, new_y);
    } else if((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && is_dragging_notif) {
        is_dragging_notif = false;
        const lv_coord_t y = lv_obj_get_y(notif_panel);

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, notif_panel);
        lv_anim_set_values(&anim, y, y > -250 ? 0 : -502);
        lv_anim_set_time(&anim, 260);
        lv_anim_set_exec_cb(&anim, anim_notif_panel_cb);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_start(&anim);
    }
}

void update_time_cb(lv_timer_t * timer) {
    LV_UNUSED(timer);

    struct tm timeinfo;
    if(!getLocalTime(&timeinfo, 10)) {
        refresh_battery_ui();
        return;
    }

    char date_str[20];
    char time_str[10];
    char week_str[10];
    static const char * week_days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

    snprintf(date_str, sizeof(date_str), "%04d/%02d/%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    snprintf(week_str, sizeof(week_str), "%s", week_days[timeinfo.tm_wday]);

    if(lock_date_label) lv_label_set_text(lock_date_label, date_str);
    if(lock_time_label) lv_label_set_text(lock_time_label, time_str);
    if(lock_week_label) lv_label_set_text(lock_week_label, week_str);
    if(status_time_label) lv_label_set_text(status_time_label, time_str);
    if(notif_time_label) lv_label_set_text(notif_time_label, time_str);
    if(sleep_time_label) lv_label_set_text(sleep_time_label, time_str);
    if(sleep_date_label) lv_label_set_text(sleep_date_label, date_str);

    const String current_alarm_key = String(date_str) + " " + time_str;
    if(alarm_enabled && !alarm_ringing && timeinfo.tm_hour == alarm_hour && timeinfo.tm_min == alarm_minute) {
        if(alarm_last_trigger_key != current_alarm_key) {
            alarm_last_trigger_key = current_alarm_key;
            trigger_alarm_alert(time_str);
        }
    }

    refresh_battery_ui();
}

void enter_sleep_screen_mode() {
    if(is_sleeping || !sleep_screen) {
        return;
    }

    is_sleeping = true;
    sleep_display_off = false;
    sleep_entered_at = millis();
    close_settings_panel();
    if(notif_panel) {
        anim_notif_panel_cb(notif_panel, -502);
    }
    refresh_sleep_icon(true);
    gfx_co5300->setBrightness(screen_brightness);
    lv_obj_clear_flag(sleep_screen, LV_OBJ_FLAG_HIDDEN);
}

void exit_sleep_screen_mode() {
    if(!is_sleeping || !sleep_screen) {
        return;
    }

    is_sleeping = false;
    sleep_display_off = false;
    sleep_entered_at = 0;
    last_activity_time = millis();
    gfx_co5300->setBrightness(screen_brightness);
    lv_obj_add_flag(sleep_screen, LV_OBJ_FLAG_HIDDEN);
}

void firefly_handle_short_press() {
    static bool last_btn_state = HIGH;
    static unsigned long btn_press_time = 0;
    const bool current_btn_state = digitalRead(0);

    if(last_btn_state == HIGH && current_btn_state == LOW) {
        btn_press_time = millis();
    }

    if(last_btn_state == LOW && current_btn_state == HIGH) {
        if(millis() - btn_press_time < 800) {
            if(settings_panel && !lv_obj_has_flag(settings_panel, LV_OBJ_FLAG_HIDDEN)) {
                if(settings_menu_container && lv_obj_has_flag(settings_menu_container, LV_OBJ_FLAG_HIDDEN)) {
                    set_settings_subpage(NULL);
                } else {
                    close_settings_panel();
                }
            } else if(is_sleeping && sleep_display_off) {
                wake_sleep_screen_from_blackout();
            } else if(is_sleeping) {
                exit_sleep_screen_mode();
            } else if(alarm_ringing) {
                dismiss_alarm_alert();
            } else if(is_on_lockscreen) {
                enter_sleep_screen_mode();
            } else if(tv_main) {
                lv_obj_set_tile_id(tv_main, 0, 0, LV_ANIM_ON);
                if(notif_panel) {
                    anim_notif_panel_cb(notif_panel, -502);
                }
            }
        }
    }

    last_btn_state = current_btn_state;
}

void firefly_handle_auto_sleep() {
    if(is_sleeping && !sleep_display_off && sleep_entered_at > 0 && millis() - sleep_entered_at >= 2000UL) {
        sleep_display_off = true;
        gfx_co5300->setBrightness(0);
        if(sleep_screen) {
            lv_obj_add_flag(sleep_screen, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if(!is_sleeping && is_on_lockscreen && auto_sleep_ms > 0) {
        if(last_activity_time == 0) {
            last_activity_time = millis();
        }
        if(millis() - last_activity_time > auto_sleep_ms) {
            enter_sleep_screen_mode();
        }
    }
}
