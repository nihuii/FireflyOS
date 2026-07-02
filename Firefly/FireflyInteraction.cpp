#include "FireflyApp.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h>

firefly::EventBus system_event_bus;

namespace {

TaskHandle_t firefly_background_task_handle = NULL;
bool firefly_background_task_running = false;
volatile uint32_t event_post_failures = 0;
uint32_t desktop_transition_released_at = 0;
uint32_t desktop_transition_max_ms = 0;

void post_background_system_event(const firefly::SystemEvent & event) {
    if(!system_event_bus.post(event)) {
        ++event_post_failures;
    }
}

bool poll_short_press_source() {
    static bool last_btn_state = HIGH;
    static unsigned long btn_press_time = 0;
    const bool current_btn_state = digitalRead(0);
    bool short_press_detected = false;

    if(last_btn_state == HIGH && current_btn_state == LOW) {
        btn_press_time = millis();
    }

    if(last_btn_state == LOW && current_btn_state == HIGH) {
        short_press_detected = (millis() - btn_press_time) < 800UL;
    }

    last_btn_state = current_btn_state;
    return short_press_detected;
}

bool should_blackout_sleep_now(unsigned long now) {
    const unsigned long entered_at = sleep_entered_at;
    return is_sleeping && !sleep_display_off && entered_at > 0 && now - entered_at >= 2000UL;
}

bool should_auto_enter_sleep_now(unsigned long now) {
    const unsigned long last_activity = last_activity_time;
    const uint32_t auto_sleep = auto_sleep_ms;

    if(is_sleeping || !is_on_lockscreen || auto_sleep == 0 || last_activity == 0) {
        return false;
    }

    return now - last_activity > auto_sleep;
}

void apply_sleep_blackout() {
    if(!is_sleeping || sleep_display_off) {
        return;
    }

    sleep_display_off = true;
    ui_state_store.setSleepState(true, true);
    firefly_board.setDisplayBrightness(0);
    glance_screen.hide();
}

void wake_sleep_screen_from_blackout();

void run_short_press_action() {
    if(tools_app.closeFlashlightFromInput()) {
        return;
    } else if(settings_panel && !lv_obj_has_flag(settings_panel, LV_OBJ_FLAG_HIDDEN)) {
        if(settings_menu_container && lv_obj_has_flag(settings_menu_container, LV_OBJ_FLAG_HIDDEN)) {
            set_settings_subpage(NULL);
        } else {
            ui_shell.back();
        }
    } else if(is_sleeping && sleep_display_off) {
        wake_sleep_screen_from_blackout();
    } else if(is_sleeping) {
        exit_sleep_screen_mode();
    } else if(alarm_ringing) {
        dismiss_alarm_alert();
    } else if(is_on_lockscreen) {
        enter_sleep_screen_mode();
    } else {
        ui_shell.back();
        if(notif_panel) {
            anim_notif_panel_cb(notif_panel, -502);
        }
    }
}

void firefly_background_task(void * parameter) {
    LV_UNUSED(parameter);

    for(;;) {
        const unsigned long now = millis();

        if(poll_short_press_source()) {
            post_background_system_event({firefly::EventType::ShortPress,
                                          0,
                                          now,
                                          firefly::EventPriority::Critical});
        }

        if(should_blackout_sleep_now(now)) {
            post_background_system_event({firefly::EventType::SleepBlackout,
                                          0,
                                          now,
                                          firefly::EventPriority::Refresh});
        } else if(should_auto_enter_sleep_now(now)) {
            post_background_system_event({firefly::EventType::EnterSleep,
                                          0,
                                          now,
                                          firefly::EventPriority::Refresh});
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

String two_digit_text(uint8_t value) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02u", value);
    return String(buf);
}

String alarm_time_text(const FireflyAlarm & alarm) {
    return two_digit_text(alarm.hour) + ":" + two_digit_text(alarm.minute);
}

String alarm_name_text(const FireflyAlarm & alarm, uint8_t slot) {
    if(alarm.name[0] != '\0') {
        return String(alarm.name);
    }

    return "Alarm " + String(slot + 1U);
}

firefly::Alarm service_alarm_from_legacy(const FireflyAlarm & legacy_alarm) {
    firefly::Alarm alarm{};
    alarm.configured = legacy_alarm.configured;
    alarm.enabled = legacy_alarm.enabled;
    alarm.hour = legacy_alarm.hour;
    alarm.minute = legacy_alarm.minute;
    alarm.days_mask = legacy_alarm.days_mask;
    alarm.ringtone = legacy_alarm.ringtone_index;
    strlcpy(alarm.name, legacy_alarm.name, sizeof(alarm.name));
    return alarm;
}

void sync_alarm_service_from_legacy() {
    for(uint8_t slot = 0; slot < FIREFLY_ALARM_SLOT_COUNT; ++slot) {
        alarm_service.set(slot, service_alarm_from_legacy(firefly_alarms[slot]));
    }
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

void update_desktop_transition_ui(lv_obj_t * active_tile) {
    const bool on_desktop = (active_tile == tile_sys);

    if(on_desktop && desktop_transition_released_at > 0) {
        const uint32_t elapsed = millis() - desktop_transition_released_at;
        if(elapsed > desktop_transition_max_ms) {
            desktop_transition_max_ms = elapsed;
        }
        desktop_transition_released_at = 0;
    }

    is_on_lockscreen = !on_desktop;
    ui_shell.syncRoute(on_desktop ? firefly::Route::Home : firefly::Route::Lock);

    if(top_status_bar) {
        if(on_desktop) {
            lv_obj_clear_flag(top_status_bar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(top_status_bar, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if(on_desktop) home_screen.show();
    else home_screen.hide();
}

void refresh_control_center_ui_impl(const firefly::BatteryState & battery) {
    LV_UNUSED(battery);
    if(settings_brightness_slider) {
        lv_slider_set_value(settings_brightness_slider, screen_brightness, LV_ANIM_OFF);
    }
    if(settings_brightness_value_label) {
        lv_label_set_text(settings_brightness_value_label, brightness_percent_text().c_str());
    }
}

void refresh_battery_details_label(const firefly::BatteryState & battery) {
    if(!settings_batt_info) {
        return;
    }

    String info = "";
    info += "Battery " + String(battery.percent) + "%\n";
    info += "Temperature " + String(battery.temperature_c) + " C\n";
    info += "Charging " + String(battery.charging ? "YES" : "NO") + "\n";
    info += "VBUS " + String(battery.vbus_present ? "YES" : "NO") + "\n";
    info += "Batt " + String(battery.battery_mv) + " mV\n";
    info += "System " + String(battery.system_mv) + " mV";
    lv_label_set_text(settings_batt_info, info.c_str());
}

void hide_charge_overlay() {
    charging_overlay_visible = false;
    ui_shell.closeOverlay(charge_overlay);
}

void show_charge_overlay(const firefly::BatteryState & battery) {
    charging_overlay_visible = true;
    charge_overlay_started_at = millis();

    if(charge_percent_label) {
        lv_label_set_text(charge_percent_label, (String(battery.percent) + "%").c_str());
    }
    if(charge_status_label) {
        lv_label_set_text(charge_status_label, battery.charging ? "Charging" : "Power Connected");
    }
    ui_shell.showOverlay(2, charge_overlay);
}

void trigger_alarm_alert(uint8_t slot, const String& current_time) {
    const FireflyAlarm & alarm = firefly_alarms[slot];
    alarm_ringing = true;

    if(is_sleeping) {
        exit_sleep_screen_mode();
    }

    if(alarm_overlay_title) {
        lv_label_set_text(alarm_overlay_title, alarm_name_text(alarm, slot).c_str());
    }
    if(alarm_overlay_detail) {
        String detail = "It is " + current_time;
        detail += "\n";
        detail += firefly_alarm_day_label(alarm.days_mask);
        detail += "  ";
        detail += firefly_alarm_ringtone_name(alarm.ringtone_index);
        detail += "\nVolume " + String(volume_level) + "%";
        lv_label_set_text(alarm_overlay_detail, detail.c_str());
    }
    ui_shell.showOverlay(4, alarm_overlay);
}

void wake_sleep_screen_from_blackout() {
    if(!is_sleeping) {
        return;
    }

    sleep_display_off = false;
    sleep_entered_at = millis();
    ui_state_store.setSleepState(true, false);
    glance_screen.show();
    ui_shell.bringAppToFront(sleep_screen);
    lv_refr_now(NULL);
    firefly_board.setDisplayBrightness(screen_brightness);
}

void refresh_alarm_card_ui(uint8_t slot) {
    if(slot >= FIREFLY_ALARM_SLOT_COUNT) {
        return;
    }

    const FireflyAlarm & alarm = firefly_alarms[slot];
    if(settings_alarm_time_labels[slot]) {
        lv_label_set_text(settings_alarm_time_labels[slot], alarm.configured ? alarm_time_text(alarm).c_str() : "--:--");
    }
    if(settings_alarm_days_labels[slot]) {
        lv_label_set_text(settings_alarm_days_labels[slot], alarm.configured ? firefly_alarm_day_label(alarm.days_mask) : "No schedule");
    }
    if(settings_alarm_name_labels[slot]) {
        lv_label_set_text(settings_alarm_name_labels[slot], alarm.configured ? alarm_name_text(alarm, slot).c_str() : "Empty alarm");
    }
    if(settings_alarm_empty_labels[slot]) {
        if(alarm.configured) {
            lv_obj_add_flag(settings_alarm_empty_labels[slot], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(settings_alarm_empty_labels[slot], "Tap + to add");
            lv_obj_clear_flag(settings_alarm_empty_labels[slot], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(settings_alarm_switches[slot]) {
        if(alarm.configured) {
            lv_obj_clear_flag(settings_alarm_switches[slot], LV_OBJ_FLAG_HIDDEN);
            if(alarm.enabled) {
                lv_obj_add_state(settings_alarm_switches[slot], LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(settings_alarm_switches[slot], LV_STATE_CHECKED);
            }
        } else {
            lv_obj_add_flag(settings_alarm_switches[slot], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void refresh_sleep_icon(bool advance) {
    if(advance) glance_screen.advanceImage();
    else glance_screen.presentCurrentImage();
}

} // namespace

void start_firefly_background_task() {
#if defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE
    firefly_background_task_running = false;
#else
    if(firefly_background_task_handle) {
        firefly_background_task_running = true;
        return;
    }

    const BaseType_t ui_core = xPortGetCoreID();
    const BaseType_t background_core = (ui_core == 0) ? 1 : 0;
    const BaseType_t result = xTaskCreatePinnedToCore(
        firefly_background_task,
        "firefly_bg",
        4096,
        NULL,
        1,
        &firefly_background_task_handle,
        background_core
    );

    firefly_background_task_running = (result == pdPASS);
    if(!firefly_background_task_running) {
        firefly_background_task_handle = NULL;
        Serial.println("Failed to start Firefly background task. Falling back to single-core polling.");
    }
#endif
}

void sync_time_to_system_from_epoch(int64_t epoch_seconds) {
    if(epoch_seconds < 0) {
        return;
    }
    setenv("TZ", "CST-8", 1);
    tzset();
    struct timeval now;
    now.tv_sec = static_cast<time_t>(epoch_seconds);
    now.tv_usec = 0;
    settimeofday(&now, NULL);
}

void load_sound_alarm_preferences() {
    volume_level = prefs.getUChar(UI_PREF_VOLUME_KEY, 50);
    if(volume_level > 100) volume_level = 100;

    bool has_configured_alarm = false;
    for(uint8_t slot = 0; slot < FIREFLY_ALARM_SLOT_COUNT; ++slot) {
        FireflyAlarm & alarm = firefly_alarms[slot];
        firefly_alarm_reset(alarm, slot);

        char key[20];
        snprintf(key, sizeof(key), "al%u_cfg", static_cast<unsigned>(slot));
        alarm.configured = prefs.getBool(key, false);
        snprintf(key, sizeof(key), "al%u_en", static_cast<unsigned>(slot));
        alarm.enabled = prefs.getBool(key, false);
        snprintf(key, sizeof(key), "al%u_hr", static_cast<unsigned>(slot));
        alarm.hour = prefs.getUChar(key, alarm.hour);
        snprintf(key, sizeof(key), "al%u_mn", static_cast<unsigned>(slot));
        alarm.minute = prefs.getUChar(key, alarm.minute);
        snprintf(key, sizeof(key), "al%u_dy", static_cast<unsigned>(slot));
        alarm.days_mask = prefs.getUChar(key, firefly_alarm_days_mask_from_option(0));
        snprintf(key, sizeof(key), "al%u_rg", static_cast<unsigned>(slot));
        alarm.ringtone_index = prefs.getUChar(key, 0);
        snprintf(key, sizeof(key), "al%u_nm", static_cast<unsigned>(slot));
        const String stored_name = prefs.getString(key, alarm.name);
        strncpy(alarm.name, stored_name.c_str(), sizeof(alarm.name) - 1U);
        alarm.name[sizeof(alarm.name) - 1U] = '\0';

        if(alarm.hour > 23) alarm.hour = 7;
        if(alarm.minute > 59) alarm.minute = 30;
        if(alarm.days_mask == 0) alarm.days_mask = firefly_alarm_days_mask_from_option(0);
        if(alarm.ringtone_index >= FIREFLY_ALARM_RINGTONE_COUNT) alarm.ringtone_index = 0;
        if(alarm.configured) {
            has_configured_alarm = true;
        }
    }

    if(!has_configured_alarm) {
        const bool legacy_enabled = prefs.getBool(UI_PREF_ALARM_ENABLED_KEY, false);
        const uint8_t legacy_hour = prefs.getUChar(UI_PREF_ALARM_HOUR_KEY, 7);
        const uint8_t legacy_minute = prefs.getUChar(UI_PREF_ALARM_MINUTE_KEY, 30);
        if(legacy_enabled) {
            FireflyAlarm & alarm = firefly_alarms[0];
            alarm.configured = true;
            alarm.enabled = true;
            alarm.hour = legacy_hour > 23 ? 7 : legacy_hour;
            alarm.minute = legacy_minute > 59 ? 30 : legacy_minute;
            alarm.days_mask = firefly_alarm_days_mask_from_option(0);
            strncpy(alarm.name, "Alarm 1", sizeof(alarm.name) - 1U);
            alarm.name[sizeof(alarm.name) - 1U] = '\0';
        }
    }

    sync_alarm_service_from_legacy();
    clear_alarm_trigger_history();
}

void save_volume_preference() {
    prefs.putUChar(UI_PREF_VOLUME_KEY, volume_level);
}

void save_alarm_preferences() {
    for(uint8_t slot = 0; slot < FIREFLY_ALARM_SLOT_COUNT; ++slot) {
        const FireflyAlarm & alarm = firefly_alarms[slot];
        char key[20];
        snprintf(key, sizeof(key), "al%u_cfg", static_cast<unsigned>(slot));
        prefs.putBool(key, alarm.configured);
        snprintf(key, sizeof(key), "al%u_en", static_cast<unsigned>(slot));
        prefs.putBool(key, alarm.enabled);
        snprintf(key, sizeof(key), "al%u_hr", static_cast<unsigned>(slot));
        prefs.putUChar(key, alarm.hour);
        snprintf(key, sizeof(key), "al%u_mn", static_cast<unsigned>(slot));
        prefs.putUChar(key, alarm.minute);
        snprintf(key, sizeof(key), "al%u_dy", static_cast<unsigned>(slot));
        prefs.putUChar(key, alarm.days_mask);
        snprintf(key, sizeof(key), "al%u_rg", static_cast<unsigned>(slot));
        prefs.putUChar(key, alarm.ringtone_index);
        snprintf(key, sizeof(key), "al%u_nm", static_cast<unsigned>(slot));
        prefs.putString(key, String(alarm.name));
    }
}

void clear_alarm_trigger_history() {
    for(uint8_t slot = 0; slot < FIREFLY_ALARM_SLOT_COUNT; ++slot) {
        firefly_alarm_last_trigger_keys[slot] = "";
    }
    alarm_service.resetTriggerHistory();
}

void open_settings_panel() {
    if(!settings_panel) {
        return;
    }
    set_settings_subpage(NULL);
    settings_app.show();
}

void close_settings_panel() {
    if(!settings_panel) {
        return;
    }
    set_settings_subpage(NULL);
    settings_app.hide();
}

void set_settings_subpage(lv_obj_t * page) {
    if(settings_alarm_editor) {
        lv_obj_add_flag(settings_alarm_editor, LV_OBJ_FLAG_HIDDEN);
    }
    if(settings_alarm_editor_keyboard) {
        lv_obj_add_flag(settings_alarm_editor_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    if(settings_menu_container) lv_obj_add_flag(settings_menu_container, LV_OBJ_FLAG_HIDDEN);
    if(settings_batt_container) lv_obj_add_flag(settings_batt_container, LV_OBJ_FLAG_HIDDEN);
    if(settings_time_container) lv_obj_add_flag(settings_time_container, LV_OBJ_FLAG_HIDDEN);
    if(settings_sound_container) lv_obj_add_flag(settings_sound_container, LV_OBJ_FLAG_HIDDEN);
    if(settings_alarm_container) lv_obj_add_flag(settings_alarm_container, LV_OBJ_FLAG_HIDDEN);
    if(settings_display_container) lv_obj_add_flag(settings_display_container, LV_OBJ_FLAG_HIDDEN);

    if(page) {
        lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    } else if(settings_menu_container) {
        lv_obj_clear_flag(settings_menu_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void refresh_runtime_status_ui() {
    const firefly::BatteryState battery = firefly_board.readBattery();
    ui_state_store.setBattery(battery);
    const firefly::SystemState state = ui_state_store.snapshot();
    control_center.refresh(state, volume_level, screen_brightness,
                           ui_state_store.revision());
    lock_screen.refresh(state);
    home_screen.refresh(state);
    calendar_app.refresh(state);
    clock_app.refresh(state);
    settings_app.refresh(state);
    tools_app.refresh(state, screen_brightness, millis());
    notification_center.refresh(state);
    ui_shell.refresh(state, ui_state_store.revision());
}

void refresh_battery_ui() {
    const firefly::BatteryState battery = firefly_board.readBattery();
    const int battery_percent = battery.percent;
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

    refresh_battery_details_label(battery);
    refresh_control_center_ui_impl(battery);
    ui_state_store.setBattery(battery);
    const firefly::SystemState state = ui_state_store.snapshot();
    control_center.refresh(state, volume_level, screen_brightness,
                           ui_state_store.revision());
    lock_screen.refresh(state);
    calendar_app.refresh(state);
    clock_app.refresh(state);
    settings_app.refresh(state);
    tools_app.refresh(state, screen_brightness, millis());
    ui_shell.refresh(state, ui_state_store.revision());
}

void refresh_sound_alarm_ui() {
    if(settings_volume_slider) {
        lv_slider_set_value(settings_volume_slider, volume_level, LV_ANIM_OFF);
    }
    if(settings_volume_value_label) {
        lv_label_set_text(settings_volume_value_label, (String(volume_level) + "%").c_str());
    }

    if(settings_alarm_summary_label) {
        lv_label_set_text(settings_alarm_summary_label, firefly_alarm_minutes_until_text(time(NULL)).c_str());
    }

    bool has_empty_slot = false;
    for(uint8_t slot = 0; slot < FIREFLY_ALARM_SLOT_COUNT; ++slot) {
        refresh_alarm_card_ui(slot);
        if(!firefly_alarms[slot].configured) {
            has_empty_slot = true;
        }
    }
    if(settings_alarm_add_button) {
        if(has_empty_slot) {
            lv_obj_clear_flag(settings_alarm_add_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(settings_alarm_add_button, LV_OBJ_FLAG_HIDDEN);
        }
    }

    refresh_runtime_status_ui();
}

void set_screen_brightness_level(uint8_t brightness) {
    if(brightness < 20) brightness = 20;
    screen_brightness = brightness;
    if(!sleep_display_off) {
        firefly_board.setDisplayBrightness(screen_brightness);
    }
    refresh_runtime_status_ui();
}

void dismiss_alarm_alert() {
    alarm_ringing = false;
    ui_shell.closeOverlay(alarm_overlay);
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

    const firefly::BatteryState battery = firefly_board.readBattery();
    const bool charging_now = battery.charging || battery.vbus_present;
    if(charging_now && !charging_last_state) {
        show_charge_overlay(battery);
    } else if(!charging_now && charging_last_state) {
        hide_charge_overlay();
    }
    charging_last_state = charging_now;

    if(!charging_overlay_visible) {
        return;
    }

    if(charge_percent_label) {
        lv_label_set_text(charge_percent_label, (String(battery.percent) + "%").c_str());
    }
    if(charge_status_label) {
        lv_label_set_text(charge_status_label, charging_now ? "Charging" : "Power Connected");
    }
}

void tv_event_cb(lv_event_t * e) {
    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * tv = lv_event_get_target(e);
    if(code == LV_EVENT_RELEASED) {
        if(is_on_lockscreen) {
            desktop_transition_released_at = millis();
        }
        return;
    }
    if(code == LV_EVENT_SCROLL_BEGIN) {
        home_screen.hide();
        return;
    }

    if(code != LV_EVENT_SCROLL_END && code != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    update_desktop_transition_ui(lv_tileview_get_tile_act(tv));
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
        lv_anim_set_time(&anim, 180);
        lv_anim_set_exec_cb(&anim, anim_notif_panel_cb);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_start(&anim);
    }
}

void update_time_cb(lv_timer_t * timer) {
    LV_UNUSED(timer);

    time_service.tick();
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo, 10)) {
        const firefly::TimeSnapshot snapshot = time_service.now();
        firefly::TimeState time_state{};
        time_state.epoch_seconds = snapshot.epoch_seconds;
        time_state.valid = snapshot.valid;
        ui_state_store.setTime(time_state);
        refresh_battery_ui();
        return;
    }

    const int64_t current_epoch = static_cast<int64_t>(time(NULL));
    firefly::TimeState time_state{};
    time_state.epoch_seconds = current_epoch;
    time_state.valid = true;
    ui_state_store.setTime(time_state);

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

    char next_alarm_text[48] = "NEXT  --:--";
    const firefly::AlarmTrigger next_alarm = alarm_service.nextTrigger(current_epoch);
    if(next_alarm.valid) {
        const time_t next_alarm_ts = static_cast<time_t>(next_alarm.epoch_seconds);
        struct tm next_alarm_tm{};
        if(localtime_r(&next_alarm_ts, &next_alarm_tm)) {
            snprintf(next_alarm_text, sizeof(next_alarm_text), "NEXT  %02d:%02d  %s",
                     next_alarm_tm.tm_hour, next_alarm_tm.tm_min,
                     alarm_name_text(firefly_alarms[next_alarm.slot], next_alarm.slot).c_str());
        }
    }
    lock_screen.setNextAlarm(next_alarm_text);

    const String current_alarm_key = String(date_str) + " " + time_str;
    if(!alarm_ringing) {
        uint8_t triggered_slot = 0;
        if(alarm_service.shouldTrigger(current_epoch, triggered_slot)) {
            firefly_alarm_last_trigger_keys[triggered_slot] =
                String(triggered_slot) + " " + current_alarm_key;
            trigger_alarm_alert(triggered_slot, time_str);
        }
    }

    refresh_sound_alarm_ui();
    refresh_battery_ui();
}

void enter_sleep_screen_mode() {
    if(is_sleeping || !sleep_screen) {
        return;
    }

    is_sleeping = true;
    sleep_display_off = false;
    sleep_entered_at = millis();
    ui_state_store.setSleepState(true, false);
    close_settings_panel();
    if(notif_panel) {
        anim_notif_panel_cb(notif_panel, -502);
    }
    refresh_sleep_icon(true);
    firefly_board.setDisplayBrightness(screen_brightness);
    glance_screen.show();
    ui_shell.bringAppToFront(sleep_screen);
}

void exit_sleep_screen_mode() {
    if(!is_sleeping || !sleep_screen) {
        return;
    }

    is_sleeping = false;
    sleep_display_off = false;
    sleep_entered_at = 0;
    ui_state_store.setSleepState(false, false);
    last_activity_time = millis();
    firefly_board.setDisplayBrightness(screen_brightness);
    glance_screen.hide();
}

void firefly_process_system_events() {
    if(!firefly_background_task_running) {
        const unsigned long now = millis();
        if(poll_short_press_source()) {
            run_short_press_action();
        }
        if(should_blackout_sleep_now(now)) {
            apply_sleep_blackout();
        } else if(should_auto_enter_sleep_now(now)) {
            enter_sleep_screen_mode();
        }
    }

    firefly::SystemEvent event{};
    while(system_event_bus.take(event)) {
        switch(event.type) {
            case firefly::EventType::ShortPress:
                run_short_press_action();
                break;
            case firefly::EventType::EnterSleep:
                if(!is_sleeping) {
                    enter_sleep_screen_mode();
                }
                break;
            case firefly::EventType::SleepBlackout:
                apply_sleep_blackout();
                break;
            default:
                break;
        }
    }
}

void firefly_process_tools_commands() {
    tools_app.tick(millis());
    firefly::ToolsCommand command{};
    while(tools_app.takeCommand(command)) {
        if(command.type == firefly::ToolsCommandType::SetBrightness) {
            set_screen_brightness_level(command.value);
        }
    }
}

void firefly_report_gate_a_diagnostics() {
    static uint32_t last_report_at = 0;
    const uint32_t now = millis();
    if(now - last_report_at < 10000UL) {
        return;
    }
    last_report_at = now;
    Serial.printf(
        "FIREFLY_GATE_A uptime_ms=%lu internal_free=%u internal_min=%u "
        "psram_free=%u event_post_failures=%lu event_queue=%u "
        "desktop_transition_max_ms=%lu\n",
        static_cast<unsigned long>(now),
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        ESP.getFreePsram(),
        static_cast<unsigned long>(event_post_failures),
        static_cast<unsigned>(system_event_bus.size()),
        static_cast<unsigned long>(desktop_transition_max_ms)
    );
}
