#include "FireflyApp.h"

namespace {

void open_settings_menu(lv_event_t * e) {
    LV_UNUSED(e);
    open_settings_panel();
    set_settings_subpage(NULL);
}

void load_time_rollers_from_current() {
    struct tm timeinfo;
    if(getLocalTime(&timeinfo, 20)) {
        lv_roller_set_selected(roller_year, timeinfo.tm_year + 1900 - 2024, LV_ANIM_OFF);
        lv_roller_set_selected(roller_month, timeinfo.tm_mon, LV_ANIM_OFF);
        lv_roller_set_selected(roller_day, timeinfo.tm_mday - 1, LV_ANIM_OFF);
        lv_roller_set_selected(roller_hour, timeinfo.tm_hour, LV_ANIM_OFF);
        lv_roller_set_selected(roller_minute, timeinfo.tm_min, LV_ANIM_OFF);
        return;
    }

    RTC_DateTime dt = rtc.getDateTime();
    if(dt.getYear() > 2023) {
        lv_roller_set_selected(roller_year, dt.getYear() - 2024, LV_ANIM_OFF);
        lv_roller_set_selected(roller_month, dt.getMonth() - 1, LV_ANIM_OFF);
        lv_roller_set_selected(roller_day, dt.getDay() - 1, LV_ANIM_OFF);
        lv_roller_set_selected(roller_hour, dt.getHour(), LV_ANIM_OFF);
        lv_roller_set_selected(roller_minute, dt.getMinute(), LV_ANIM_OFF);
    }
}

void open_sound_page(lv_event_t * e) {
    LV_UNUSED(e);
    open_settings_panel();
    refresh_sound_alarm_ui();
    set_settings_subpage(settings_sound_container);
}

void open_time_page(lv_event_t * e) {
    LV_UNUSED(e);
    open_settings_panel();
    load_time_rollers_from_current();
    set_settings_subpage(settings_time_container);
}

void open_battery_page(lv_event_t * e) {
    LV_UNUSED(e);
    open_settings_panel();
    refresh_battery_ui();
    set_settings_subpage(settings_batt_container);
}

void open_display_page(lv_event_t * e) {
    LV_UNUSED(e);
    open_settings_panel();
    set_settings_subpage(settings_display_container);
}

void save_time_from_rollers(lv_event_t * e) {
    LV_UNUSED(e);
    char buf[10];
    lv_roller_get_selected_str(roller_year, buf, sizeof(buf)); const int year = atoi(buf);
    lv_roller_get_selected_str(roller_month, buf, sizeof(buf)); const int month = atoi(buf);
    lv_roller_get_selected_str(roller_day, buf, sizeof(buf)); const int day = atoi(buf);
    lv_roller_get_selected_str(roller_hour, buf, sizeof(buf)); const int hour = atoi(buf);
    lv_roller_get_selected_str(roller_minute, buf, sizeof(buf)); const int minute = atoi(buf);

    rtc.setDateTime(year, month, day, hour, minute, 0);
    sync_time_to_system_from_rtc(rtc.getDateTime());
    alarm_last_trigger_key = "";
    update_time_cb(NULL);
    set_settings_subpage(NULL);
}

void load_time_from_rtc(lv_event_t * e) {
    LV_UNUSED(e);
    RTC_DateTime dt = rtc.getDateTime();
    if(dt.getYear() <= 2023) {
        return;
    }

    sync_time_to_system_from_rtc(dt);
    alarm_last_trigger_key = "";
    load_time_rollers_from_current();
    update_time_cb(NULL);
}

void dismiss_alarm_cb(lv_event_t * e) {
    LV_UNUSED(e);
    dismiss_alarm_alert();
}

void close_sleep_screen_cb(lv_event_t * e) {
    LV_UNUSED(e);
    if(is_sleeping) {
        exit_sleep_screen_mode();
    }
}

void slider_volume_cb(lv_event_t * e) {
    volume_level = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    save_volume_preference();
    refresh_sound_alarm_ui();
}

void slider_brightness_cb(lv_event_t * e) {
    set_screen_brightness_level((uint8_t)lv_slider_get_value(lv_event_get_target(e)));
}

void alarm_switch_cb(lv_event_t * e) {
    alarm_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    save_alarm_preferences();
    refresh_sound_alarm_ui();
}

void alarm_hour_cb(lv_event_t * e) {
    alarm_hour = (uint8_t)lv_roller_get_selected(lv_event_get_target(e));
    save_alarm_preferences();
    refresh_sound_alarm_ui();
}

void alarm_minute_cb(lv_event_t * e) {
    alarm_minute = (uint8_t)lv_roller_get_selected(lv_event_get_target(e));
    save_alarm_preferences();
    refresh_sound_alarm_ui();
}

void auto_sleep_cb(lv_event_t * e) {
    static const uint32_t timeouts[] = {0, 15000, 30000, 60000, 120000, 300000};
    const uint16_t selected = lv_roller_get_selected(lv_event_get_target(e));
    auto_sleep_ms = timeouts[selected];
}

} // namespace

void build_firefly_os() {
    const lv_color_t settings_surface = settings_theme_surface;
    const lv_color_t settings_surface_alt = settings_theme_surface_alt;
    const lv_color_t settings_text_primary = settings_theme_text_primary;
    const lv_color_t settings_text_secondary = settings_theme_text_secondary;
    const lv_color_t settings_action = settings_theme_action;
    const lv_color_t control_bg = lv_color_hex(0xEAF8FB);
    const lv_color_t control_card = lv_color_hex(0xD9F1F6);
    const lv_color_t control_text = lv_color_hex(0x0E4C5A);
    const lv_color_t control_subtext = lv_color_hex(0x3A6F7A);
    const lv_coord_t settings_content_width = LCD_WIDTH - 32;
    const lv_coord_t settings_page_top = 92;
    const lv_coord_t settings_page_height = LCD_HEIGHT - settings_page_top;
    const lv_coord_t settings_half_button_width = (settings_content_width - 10) / 2;

    auto style_card = [&](lv_obj_t * obj, lv_color_t color, lv_coord_t radius) {
        lv_obj_set_style_radius(obj, radius, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_bg_color(obj, color, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    };

    auto style_settings_card = [&](lv_obj_t * obj, lv_color_t color, lv_coord_t radius, lv_opa_t opacity = LV_OPA_80) {
        style_card(obj, color, radius);
        lv_obj_set_style_bg_opa(obj, opacity, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_color(obj, settings_theme_accent, 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_30, 0);
    };

    auto style_slider = [&](lv_obj_t * slider, lv_color_t track, lv_color_t indicator, lv_color_t knob) {
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(slider, 0, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(slider, 0, LV_PART_KNOB);
        lv_obj_set_style_bg_color(slider, track, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, indicator, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, knob, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
    };

    auto style_switch = [&](lv_obj_t * sw) {
        lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(sw, 0, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(sw, 0, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(sw, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, settings_surface, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(sw, LV_OPA_70, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sw, settings_action, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(sw, LV_OPA_90, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_shadow_width(sw, 0, LV_PART_KNOB);
    };

    auto style_roller = [&](lv_obj_t * roller, lv_coord_t width, lv_coord_t height) {
        lv_obj_set_size(roller, width, height);
        lv_obj_set_style_radius(roller, 18, LV_PART_MAIN);
        lv_obj_set_style_border_width(roller, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(roller, settings_surface_alt, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(roller, LV_OPA_80, LV_PART_MAIN);
        lv_obj_set_style_text_color(roller, settings_text_secondary, LV_PART_MAIN);
        lv_obj_set_style_text_font(roller, &lv_font_montserrat_24, LV_PART_MAIN);
        lv_obj_set_style_pad_top(roller, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(roller, 10, LV_PART_MAIN);
        lv_obj_set_style_bg_color(roller, settings_action, LV_PART_SELECTED);
        lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_SELECTED);
        lv_obj_set_style_text_color(roller, settings_text_primary, LV_PART_SELECTED);
        lv_obj_set_style_border_width(roller, 0, LV_PART_SELECTED);
    };

    auto create_menu_button = [&](lv_coord_t y, const char * text, lv_event_cb_t cb) {
        lv_obj_t * button = lv_btn_create(settings_menu_container);
        lv_obj_set_size(button, settings_content_width, 58);
        lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
        style_settings_card(button, settings_surface_alt, 22, LV_OPA_80);
        lv_obj_t * label = lv_label_create(button);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(label, settings_text_primary, 0);
        lv_label_set_text(label, text);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 18, 0);
        lv_obj_t * arrow = lv_label_create(button);
        lv_obj_set_style_text_color(arrow, settings_text_secondary, 0);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -18, 0);
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    };

    auto create_desktop_icon = [&](lv_coord_t x, lv_coord_t y, const char * symbol, const char * text, lv_event_cb_t cb, lv_color_t color) {
        lv_obj_t * app = lv_obj_create(desktop_icon_layer);
        lv_obj_set_size(app, 82, 110);
        lv_obj_align(app, LV_ALIGN_TOP_MID, x, y);
        lv_obj_set_style_bg_opa(app, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(app, 0, 0);
        lv_obj_set_style_pad_all(app, 0, 0);
        lv_obj_clear_flag(app, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * button = lv_btn_create(app);
        lv_obj_set_size(button, 72, 72);
        lv_obj_align(button, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(button, 24, 0);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_border_color(button, lv_color_white(), 0);
        lv_obj_set_style_border_opa(button, 70, 0);
        lv_obj_set_style_bg_color(button, color, 0);
        lv_obj_set_style_bg_opa(button, 170, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t * icon = lv_label_create(button);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(icon, lv_color_white(), 0);
        lv_label_set_text(icon, symbol);
        lv_obj_center(icon);

        lv_obj_t * label = lv_label_create(app);
        lv_obj_set_width(label, 82);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xF6FBFF), 0);
        lv_obj_set_style_text_opa(label, 235, 0);
        lv_label_set_text(label, text);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);
    };

    scr_firefly = lv_obj_create(NULL);
    lv_obj_set_style_pad_all(scr_firefly, 0, 0);
    lv_obj_set_style_border_width(scr_firefly, 0, 0);
    lv_obj_set_style_bg_color(scr_firefly, lv_color_hex(0x071018), 0);
    lv_obj_set_style_bg_opa(scr_firefly, LV_OPA_COVER, 0);

    tv_main = lv_tileview_create(scr_firefly);
    lv_obj_set_size(tv_main, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_opa(tv_main, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tv_main, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tv_main, tv_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(tv_main, tv_event_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(tv_main, tv_event_cb, LV_EVENT_SCROLL_END, NULL);

    tile_lock = lv_tileview_add_tile(tv_main, 0, 0, LV_DIR_BOTTOM);
    lv_obj_set_style_bg_opa(tile_lock, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tile_lock, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * lock_wallpaper = lv_img_create(tile_lock);
    lv_img_set_src(lock_wallpaper, &lock_wallpaper_firefly_0);
    lv_obj_align(lock_wallpaper, LV_ALIGN_CENTER, 0, 0);

    lock_date_label = lv_label_create(tile_lock);
    lv_obj_set_style_text_font(lock_date_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lock_date_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lock_date_label, "----/--/--");
    lv_obj_align(lock_date_label, LV_ALIGN_TOP_MID, 0, 286);

    lock_time_label = lv_label_create(tile_lock);
    lv_obj_set_style_text_font(lock_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lock_time_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lock_time_label, "--:--");
    lv_obj_align(lock_time_label, LV_ALIGN_TOP_MID, 0, 322);

    lock_week_label = lv_label_create(tile_lock);
    lv_obj_set_style_text_font(lock_week_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lock_week_label, lv_color_hex(0xC5EAF1), 0);
    lv_label_set_text(lock_week_label, "---");
    lv_obj_align(lock_week_label, LV_ALIGN_TOP_MID, 0, 386);

    lv_obj_t * lock_hint = lv_label_create(tile_lock);
    lv_obj_set_style_text_color(lock_hint, lv_color_hex(0x8DB7C1), 0);
    lv_label_set_text(lock_hint, "Swipe up");
    lv_obj_align(lock_hint, LV_ALIGN_BOTTOM_MID, 0, -24);

    tile_sys = lv_tileview_add_tile(tv_main, 0, 1, LV_DIR_NONE);
    lv_obj_set_style_bg_opa(tile_sys, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tile_sys, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * desktop_wallpaper = lv_img_create(tile_sys);
    lv_img_set_src(desktop_wallpaper, &desktop_wallpaper_firefly_1);
    lv_obj_align(desktop_wallpaper, LV_ALIGN_CENTER, 0, 0);

    desktop_icon_layer = lv_obj_create(tile_sys);
    lv_obj_set_size(desktop_icon_layer, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_opa(desktop_icon_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(desktop_icon_layer, 0, 0);
    lv_obj_set_style_pad_all(desktop_icon_layer, 0, 0);
    lv_obj_set_style_radius(desktop_icon_layer, 0, 0);
    lv_obj_clear_flag(desktop_icon_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(desktop_icon_layer, LV_OBJ_FLAG_HIDDEN);

    create_desktop_icon(-150, 88, LV_SYMBOL_SETTINGS, "Settings", open_settings_menu, lv_color_hex(0x3D6AF2));

    notif_panel = lv_obj_create(scr_firefly);
    lv_obj_set_size(notif_panel, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_pad_all(notif_panel, 0, 0);
    lv_obj_set_style_border_width(notif_panel, 0, 0);
    lv_obj_set_style_radius(notif_panel, 0, 0);
    lv_obj_set_style_bg_color(notif_panel, control_bg, 0);
    lv_obj_set_style_bg_opa(notif_panel, 242, 0);
    lv_obj_set_y(notif_panel, -LCD_HEIGHT);
    lv_obj_add_flag(notif_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(notif_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(notif_panel, status_drag_cb, LV_EVENT_ALL, NULL);

    lv_obj_t * notif_handle = lv_obj_create(notif_panel);
    lv_obj_set_size(notif_handle, 84, 6);
    lv_obj_align(notif_handle, LV_ALIGN_TOP_MID, 0, 12);
    style_card(notif_handle, lv_color_hex(0x97DDE9), 8);

    notif_detail_label = lv_label_create(notif_panel);
    lv_obj_set_width(notif_detail_label, 340);
    lv_label_set_long_mode(notif_detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(notif_detail_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(notif_detail_label, control_subtext, 0);
    lv_label_set_text(notif_detail_label, "Battery --%\nVolume and brightness");
    lv_obj_align(notif_detail_label, LV_ALIGN_TOP_LEFT, 34, 90);
    lv_obj_t * volume_card = lv_obj_create(notif_panel);
    lv_obj_set_size(volume_card, 354, 88);
    lv_obj_align(volume_card, LV_ALIGN_TOP_MID, 0, 182);
    style_card(volume_card, control_card, 28);
    lv_obj_set_style_pad_all(volume_card, 16, 0);
    lv_obj_clear_flag(volume_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * volume_title = lv_label_create(volume_card);
    lv_obj_set_style_text_font(volume_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(volume_title, control_text, 0);
    lv_label_set_text(volume_title, LV_SYMBOL_VOLUME_MAX " Volume");
    lv_obj_align(volume_title, LV_ALIGN_TOP_LEFT, 0, 0);

    notif_volume_value_label = lv_label_create(volume_card);
    lv_obj_set_style_text_color(notif_volume_value_label, control_subtext, 0);
    lv_label_set_text(notif_volume_value_label, "50%");
    lv_obj_align(notif_volume_value_label, LV_ALIGN_TOP_RIGHT, 0, 4);

    notif_volume_slider = lv_slider_create(volume_card);
    lv_obj_set_size(notif_volume_slider, 320, 24);
    lv_obj_align(notif_volume_slider, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_slider_set_range(notif_volume_slider, 0, 100);
    lv_slider_set_value(notif_volume_slider, volume_level, LV_ANIM_OFF);
    style_slider(notif_volume_slider, lv_color_hex(0xC7E9F0), settings_action, lv_color_white());
    lv_obj_add_event_cb(notif_volume_slider, slider_volume_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * brightness_card = lv_obj_create(notif_panel);
    lv_obj_set_size(brightness_card, 354, 88);
    lv_obj_align(brightness_card, LV_ALIGN_TOP_MID, 0, 290);
    style_card(brightness_card, control_card, 28);
    lv_obj_set_style_pad_all(brightness_card, 16, 0);
    lv_obj_clear_flag(brightness_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * brightness_title = lv_label_create(brightness_card);
    lv_obj_set_style_text_font(brightness_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(brightness_title, control_text, 0);
    lv_label_set_text(brightness_title, LV_SYMBOL_IMAGE " Brightness");
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 0, 0);

    notif_brightness_value_label = lv_label_create(brightness_card);
    lv_obj_set_style_text_color(notif_brightness_value_label, control_subtext, 0);
    lv_label_set_text(notif_brightness_value_label, "50%");
    lv_obj_align(notif_brightness_value_label, LV_ALIGN_TOP_RIGHT, 0, 4);

    notif_brightness_slider = lv_slider_create(brightness_card);
    lv_obj_set_size(notif_brightness_slider, 320, 24);
    lv_obj_align(notif_brightness_slider, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_slider_set_range(notif_brightness_slider, 20, 255);
    lv_slider_set_value(notif_brightness_slider, screen_brightness, LV_ANIM_OFF);
    style_slider(notif_brightness_slider, lv_color_hex(0xC7E9F0), settings_action, lv_color_white());
    lv_obj_add_event_cb(notif_brightness_slider, slider_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    top_status_bar = lv_obj_create(scr_firefly);
    lv_obj_set_size(top_status_bar, LCD_WIDTH, 60);
    lv_obj_align(top_status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(top_status_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_status_bar, 0, 0);
    lv_obj_set_style_pad_all(top_status_bar, 0, 0);
    lv_obj_add_flag(top_status_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(top_status_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(top_status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(top_status_bar, status_drag_cb, LV_EVENT_ALL, NULL);

    status_battery_icon = lv_label_create(top_status_bar);
    lv_obj_set_style_text_color(status_battery_icon, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(status_battery_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_align(status_battery_icon, LV_ALIGN_RIGHT_MID, -18, 0);

    status_time_label = lv_label_create(top_status_bar);
    lv_obj_set_style_text_font(status_time_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(status_time_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(status_time_label, "--:--");
    lv_obj_align(status_time_label, LV_ALIGN_TOP_LEFT, 48, 15);

    notif_time_label = lv_label_create(top_status_bar);
    lv_obj_set_style_text_font(notif_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(notif_time_label, control_text, 0);
    lv_label_set_text(notif_time_label, "--:--");
    lv_obj_align(notif_time_label, LV_ALIGN_TOP_LEFT, 50, 20);
    lv_obj_set_style_text_opa(notif_time_label, 0, 0);

    settings_panel = lv_obj_create(scr_firefly);
    lv_obj_set_size(settings_panel, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_opa(settings_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings_panel, 0, 0);
    lv_obj_set_style_radius(settings_panel, 0, 0);
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * settings_wallpaper = lv_img_create(settings_panel);
    lv_img_set_src(settings_wallpaper, &settings_wallpaper_firefly_2);
    lv_obj_align(settings_wallpaper, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * settings_shell = lv_obj_create(settings_panel);
    lv_obj_set_size(settings_shell, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(settings_shell, LV_ALIGN_CENTER, 0, 0);
    style_settings_card(settings_shell, settings_surface, 0, 108);
    lv_obj_set_style_border_width(settings_shell, 0, 0);
    lv_obj_set_style_pad_all(settings_shell, 0, 0);
    lv_obj_clear_flag(settings_shell, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * settings_header = lv_label_create(settings_shell);
    lv_obj_set_style_text_font(settings_header, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(settings_header, settings_text_primary, 0);
    lv_label_set_text(settings_header, "Settings");
    lv_obj_align(settings_header, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t * settings_back = lv_label_create(settings_shell);
    lv_obj_set_style_text_font(settings_back, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(settings_back, settings_action, 0);
    lv_label_set_text(settings_back, LV_SYMBOL_LEFT " Back");
    lv_obj_align(settings_back, LV_ALIGN_TOP_LEFT, 18, 24);
    lv_obj_add_flag(settings_back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(settings_back, 24);
    lv_obj_add_event_cb(settings_back, [](lv_event_t * e) {
        LV_UNUSED(e);
        if(settings_menu_container && lv_obj_has_flag(settings_menu_container, LV_OBJ_FLAG_HIDDEN)) {
            set_settings_subpage(NULL);
        } else {
            close_settings_panel();
        }
    }, LV_EVENT_CLICKED, NULL);

    settings_menu_container = lv_obj_create(settings_shell);
    settings_batt_container = lv_obj_create(settings_shell);
    settings_time_container = lv_obj_create(settings_shell);
    settings_sound_container = lv_obj_create(settings_shell);
    settings_display_container = lv_obj_create(settings_shell);

    lv_obj_t * pages[] = {settings_menu_container, settings_batt_container, settings_time_container, settings_sound_container, settings_display_container};
    for(uint8_t i = 0; i < 5; ++i) {
        lv_obj_set_size(pages[i], LCD_WIDTH, settings_page_height);
        lv_obj_align(pages[i], LV_ALIGN_TOP_MID, 0, settings_page_top);
        lv_obj_set_style_bg_opa(pages[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pages[i], 0, 0);
        lv_obj_set_style_pad_all(pages[i], 0, 0);
        lv_obj_set_scrollbar_mode(pages[i], LV_SCROLLBAR_MODE_OFF);
    }
    lv_obj_add_flag(settings_batt_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settings_time_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settings_sound_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settings_display_container, LV_OBJ_FLAG_HIDDEN);

    create_menu_button(12, LV_SYMBOL_AUDIO "  Sound & Alarm", open_sound_page);
    create_menu_button(84, LV_SYMBOL_EDIT "  Time & Date", open_time_page);
    create_menu_button(156, LV_SYMBOL_BATTERY_FULL "  Battery & Power", open_battery_page);
    create_menu_button(228, LV_SYMBOL_IMAGE "  Display & Sleep", open_display_page);

    settings_batt_icon = lv_label_create(settings_batt_container);
    lv_obj_set_style_text_font(settings_batt_icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(settings_batt_icon, lv_color_hex(0x74F7A3), 0);
    lv_label_set_text(settings_batt_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_align(settings_batt_icon, LV_ALIGN_TOP_MID, 0, 20);

    settings_batt_info = lv_label_create(settings_batt_container);
    lv_obj_set_width(settings_batt_info, settings_content_width - 24);
    lv_label_set_long_mode(settings_batt_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(settings_batt_info, settings_text_primary, 0);
    lv_label_set_text(settings_batt_info, "Loading...");
    lv_obj_align(settings_batt_info, LV_ALIGN_TOP_LEFT, 28, 96);

    lv_obj_t * date_card = lv_obj_create(settings_time_container);
    lv_obj_set_size(date_card, settings_content_width, 132);
    lv_obj_align(date_card, LV_ALIGN_TOP_MID, 0, 18);
    style_settings_card(date_card, settings_surface_alt, 22, LV_OPA_80);
    lv_obj_set_style_pad_all(date_card, 12, 0);
    lv_obj_clear_flag(date_card, LV_OBJ_FLAG_SCROLLABLE);

    roller_year = lv_roller_create(date_card);
    lv_roller_set_options(roller_year, "2024\n2025\n2026\n2027\n2028\n2029\n2030", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_year, 3);
    style_roller(roller_year, 104, 92);
    lv_obj_align(roller_year, LV_ALIGN_LEFT_MID, 0, 0);

    roller_month = lv_roller_create(date_card);
    lv_roller_set_options(roller_month, "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_month, 3);
    style_roller(roller_month, 86, 92);
    lv_obj_align(roller_month, LV_ALIGN_CENTER, 0, 0);

    roller_day = lv_roller_create(date_card);
    lv_roller_set_options(roller_day, "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_day, 3);
    style_roller(roller_day, 86, 92);
    lv_obj_align(roller_day, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t * clock_card = lv_obj_create(settings_time_container);
    lv_obj_set_size(clock_card, settings_content_width, 122);
    lv_obj_align(clock_card, LV_ALIGN_TOP_MID, 0, 168);
    style_settings_card(clock_card, settings_surface_alt, 22, LV_OPA_80);
    lv_obj_set_style_pad_all(clock_card, 12, 0);
    lv_obj_clear_flag(clock_card, LV_OBJ_FLAG_SCROLLABLE);

    roller_hour = lv_roller_create(clock_card);
    lv_roller_set_options(roller_hour, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_hour, 3);
    style_roller(roller_hour, 110, 92);
    lv_obj_align(roller_hour, LV_ALIGN_CENTER, -72, 0);

    lv_obj_t * colon = lv_label_create(clock_card);
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(colon, settings_action, 0);
    lv_label_set_text(colon, ":");
    lv_obj_align(colon, LV_ALIGN_CENTER, 0, -4);

    roller_minute = lv_roller_create(clock_card);
    lv_roller_set_options(roller_minute, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_minute, 3);
    style_roller(roller_minute, 110, 92);
    lv_obj_align(roller_minute, LV_ALIGN_CENTER, 72, 0);

    lv_obj_t * btn_save_time = lv_btn_create(settings_time_container);
    lv_obj_set_size(btn_save_time, settings_half_button_width, 46);
    lv_obj_align(btn_save_time, LV_ALIGN_BOTTOM_LEFT, 16, -12);
    style_settings_card(btn_save_time, settings_action, 20, LV_OPA_90);
    lv_obj_add_event_cb(btn_save_time, save_time_from_rollers, LV_EVENT_CLICKED, NULL);
    lv_obj_t * btn_save_time_label = lv_label_create(btn_save_time);
    lv_obj_set_style_text_color(btn_save_time_label, settings_text_primary, 0);
    lv_label_set_text(btn_save_time_label, "Save Time");
    lv_obj_center(btn_save_time_label);

    lv_obj_t * btn_load_rtc = lv_btn_create(settings_time_container);
    lv_obj_set_size(btn_load_rtc, settings_half_button_width, 46);
    lv_obj_align(btn_load_rtc, LV_ALIGN_BOTTOM_RIGHT, -16, -12);
    style_settings_card(btn_load_rtc, settings_surface_alt, 20, LV_OPA_80);
    lv_obj_add_event_cb(btn_load_rtc, load_time_from_rtc, LV_EVENT_CLICKED, NULL);
    lv_obj_t * btn_load_rtc_label = lv_label_create(btn_load_rtc);
    lv_obj_set_style_text_color(btn_load_rtc_label, settings_text_primary, 0);
    lv_label_set_text(btn_load_rtc_label, LV_SYMBOL_REFRESH " Load RTC");
    lv_obj_center(btn_load_rtc_label);

    settings_alarm_status_label = lv_label_create(settings_sound_container);
    lv_obj_set_style_text_font(settings_alarm_status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(settings_alarm_status_label, settings_text_primary, 0);
    lv_label_set_text(settings_alarm_status_label, "Alarm disabled");
    lv_obj_align(settings_alarm_status_label, LV_ALIGN_TOP_LEFT, 18, 8);

    settings_alarm_time_label = lv_label_create(settings_sound_container);
    lv_obj_set_style_text_color(settings_alarm_time_label, settings_text_secondary, 0);
    lv_label_set_text(settings_alarm_time_label, "Time 07:30");
    lv_obj_align(settings_alarm_time_label, LV_ALIGN_TOP_LEFT, 18, 42);

    lv_obj_t * sound_volume_title = lv_label_create(settings_sound_container);
    lv_obj_set_style_text_font(sound_volume_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sound_volume_title, settings_text_primary, 0);
    lv_label_set_text(sound_volume_title, LV_SYMBOL_VOLUME_MAX " Volume");
    lv_obj_align(sound_volume_title, LV_ALIGN_TOP_LEFT, 18, 82);

    settings_volume_value_label = lv_label_create(settings_sound_container);
    lv_obj_set_style_text_color(settings_volume_value_label, settings_text_secondary, 0);
    lv_label_set_text(settings_volume_value_label, "50%");
    lv_obj_align(settings_volume_value_label, LV_ALIGN_TOP_RIGHT, -18, 88);

    settings_volume_slider = lv_slider_create(settings_sound_container);
    lv_obj_set_size(settings_volume_slider, settings_content_width, 30);
    lv_obj_align(settings_volume_slider, LV_ALIGN_TOP_MID, 0, 122);
    lv_slider_set_range(settings_volume_slider, 0, 100);
    lv_slider_set_value(settings_volume_slider, volume_level, LV_ANIM_OFF);
    style_slider(settings_volume_slider, settings_surface_alt, settings_action, lv_color_white());
    lv_obj_set_style_bg_opa(settings_volume_slider, LV_OPA_80, LV_PART_MAIN);
    lv_obj_add_event_cb(settings_volume_slider, slider_volume_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * alarm_switch_card = lv_obj_create(settings_sound_container);
    lv_obj_set_size(alarm_switch_card, settings_content_width, 58);
    lv_obj_align(alarm_switch_card, LV_ALIGN_TOP_MID, 0, 172);
    style_settings_card(alarm_switch_card, settings_surface_alt, 20, LV_OPA_80);
    lv_obj_set_style_pad_left(alarm_switch_card, 16, 0);
    lv_obj_set_style_pad_right(alarm_switch_card, 16, 0);
    lv_obj_clear_flag(alarm_switch_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * alarm_switch_label = lv_label_create(alarm_switch_card);
    lv_obj_set_style_text_color(alarm_switch_label, settings_text_primary, 0);
    lv_label_set_text(alarm_switch_label, LV_SYMBOL_BELL " Enable Alarm");
    lv_obj_align(alarm_switch_label, LV_ALIGN_LEFT_MID, 0, 0);

    settings_alarm_switch = lv_switch_create(alarm_switch_card);
    lv_obj_align(settings_alarm_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    style_switch(settings_alarm_switch);
    lv_obj_add_event_cb(settings_alarm_switch, alarm_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * alarm_time_title = lv_label_create(settings_sound_container);
    lv_obj_set_style_text_font(alarm_time_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(alarm_time_title, settings_text_primary, 0);
    lv_label_set_text(alarm_time_title, "Alarm Time");
    lv_obj_align(alarm_time_title, LV_ALIGN_TOP_LEFT, 18, 248);

    roller_alarm_hour = lv_roller_create(settings_sound_container);
    lv_roller_set_options(roller_alarm_hour, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_alarm_hour, 3);
    style_roller(roller_alarm_hour, 104, 108);
    lv_obj_align(roller_alarm_hour, LV_ALIGN_TOP_MID, -60, 282);
    lv_obj_add_event_cb(roller_alarm_hour, alarm_hour_cb, LV_EVENT_VALUE_CHANGED, NULL);

    roller_alarm_minute = lv_roller_create(settings_sound_container);
    lv_roller_set_options(roller_alarm_minute, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_alarm_minute, 3);
    style_roller(roller_alarm_minute, 104, 108);
    lv_obj_align(roller_alarm_minute, LV_ALIGN_TOP_MID, 60, 282);
    lv_obj_add_event_cb(roller_alarm_minute, alarm_minute_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * display_title = lv_label_create(settings_display_container);
    lv_obj_set_style_text_font(display_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(display_title, settings_text_primary, 0);
    lv_label_set_text(display_title, LV_SYMBOL_IMAGE " Brightness");
    lv_obj_align(display_title, LV_ALIGN_TOP_LEFT, 18, 12);

    settings_brightness_value_label = lv_label_create(settings_display_container);
    lv_obj_set_style_text_color(settings_brightness_value_label, settings_text_secondary, 0);
    lv_label_set_text(settings_brightness_value_label, "50%");
    lv_obj_align(settings_brightness_value_label, LV_ALIGN_TOP_RIGHT, -18, 18);

    settings_brightness_slider = lv_slider_create(settings_display_container);
    lv_obj_set_size(settings_brightness_slider, settings_content_width, 30);
    lv_obj_align(settings_brightness_slider, LV_ALIGN_TOP_MID, 0, 58);
    lv_slider_set_range(settings_brightness_slider, 20, 255);
    lv_slider_set_value(settings_brightness_slider, screen_brightness, LV_ANIM_OFF);
    style_slider(settings_brightness_slider, settings_surface_alt, settings_action, lv_color_white());
    lv_obj_set_style_bg_opa(settings_brightness_slider, LV_OPA_80, LV_PART_MAIN);
    lv_obj_add_event_cb(settings_brightness_slider, slider_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * sleep_title = lv_label_create(settings_display_container);
    lv_obj_set_style_text_font(sleep_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sleep_title, settings_text_primary, 0);
    lv_label_set_text(sleep_title, LV_SYMBOL_POWER " Auto Sleep");
    lv_obj_align(sleep_title, LV_ALIGN_TOP_LEFT, 18, 132);

    settings_sleep_roller = lv_roller_create(settings_display_container);
    lv_roller_set_options(settings_sleep_roller, "OFF\n15 sec\n30 sec\n1 min\n2 min\n5 min", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(settings_sleep_roller, 3);
    lv_roller_set_selected(settings_sleep_roller, 2, LV_ANIM_OFF);
    style_roller(settings_sleep_roller, settings_content_width, 126);
    lv_obj_align(settings_sleep_roller, LV_ALIGN_TOP_MID, 0, 172);
    lv_obj_add_event_cb(settings_sleep_roller, auto_sleep_cb, LV_EVENT_VALUE_CHANGED, NULL);

    alarm_overlay = lv_obj_create(scr_firefly);
    lv_obj_set_size(alarm_overlay, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(alarm_overlay, lv_color_hex(0x040608), 0);
    lv_obj_set_style_bg_opa(alarm_overlay, 235, 0);
    lv_obj_set_style_border_width(alarm_overlay, 0, 0);
    lv_obj_set_style_radius(alarm_overlay, 0, 0);
    lv_obj_add_flag(alarm_overlay, LV_OBJ_FLAG_HIDDEN);

    alarm_overlay_title = lv_label_create(alarm_overlay);
    lv_obj_set_style_text_font(alarm_overlay_title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(alarm_overlay_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(alarm_overlay_title, "Alarm");
    lv_obj_align(alarm_overlay_title, LV_ALIGN_CENTER, 0, -90);

    alarm_overlay_detail = lv_label_create(alarm_overlay);
    lv_obj_set_width(alarm_overlay_detail, 320);
    lv_obj_set_style_text_align(alarm_overlay_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(alarm_overlay_detail, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(alarm_overlay_detail, lv_color_hex(0xC3CDD8), 0);
    lv_label_set_text(alarm_overlay_detail, "It is 07:30\nVolume 50%");
    lv_obj_align(alarm_overlay_detail, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t * alarm_stop_button = lv_btn_create(alarm_overlay);
    lv_obj_set_size(alarm_stop_button, 220, 54);
    lv_obj_align(alarm_stop_button, LV_ALIGN_CENTER, 0, 108);
    style_card(alarm_stop_button, settings_action, 22);
    lv_obj_add_event_cb(alarm_stop_button, dismiss_alarm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * alarm_stop_label = lv_label_create(alarm_stop_button);
    lv_obj_set_style_text_color(alarm_stop_label, settings_text_primary, 0);
    lv_label_set_text(alarm_stop_label, "Dismiss Alarm");
    lv_obj_center(alarm_stop_label);

    charge_overlay = lv_obj_create(scr_firefly);
    lv_obj_set_size(charge_overlay, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(charge_overlay, lv_color_hex(0x071018), 0);
    lv_obj_set_style_bg_opa(charge_overlay, 235, 0);
    lv_obj_set_style_border_width(charge_overlay, 0, 0);
    lv_obj_set_style_radius(charge_overlay, 0, 0);
    lv_obj_add_flag(charge_overlay, LV_OBJ_FLAG_HIDDEN);

    charge_status_label = lv_label_create(charge_overlay);
    lv_obj_set_style_text_font(charge_status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(charge_status_label, lv_color_hex(0xB8ECF5), 0);
    lv_label_set_text(charge_status_label, "Charging");
    lv_obj_align(charge_status_label, LV_ALIGN_CENTER, 0, -22);

    charge_percent_label = lv_label_create(charge_overlay);
    lv_obj_set_style_text_font(charge_percent_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(charge_percent_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(charge_percent_label, "0%");
    lv_obj_align(charge_percent_label, LV_ALIGN_CENTER, 0, 34);

    sleep_screen = lv_obj_create(scr_firefly);
    lv_obj_set_size(sleep_screen, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(sleep_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(sleep_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sleep_screen, 0, 0);
    lv_obj_set_style_radius(sleep_screen, 0, 0);
    lv_obj_add_flag(sleep_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(sleep_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sleep_screen, close_sleep_screen_cb, LV_EVENT_CLICKED, NULL);

    sleep_icon_img = lv_img_create(sleep_screen);
    lv_img_set_src(sleep_icon_img, get_sleep_icon_by_index(0));
    lv_obj_align(sleep_icon_img, LV_ALIGN_TOP_MID, 0, 44);

    sleep_time_label = lv_label_create(sleep_screen);
    lv_obj_set_style_text_font(sleep_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(sleep_time_label, lv_color_hex(0x5FE7C7), 0);
    lv_label_set_text(sleep_time_label, "--:--");
    lv_obj_align(sleep_time_label, LV_ALIGN_TOP_MID, 0, 280);

    sleep_date_label = lv_label_create(sleep_screen);
    lv_obj_set_style_text_font(sleep_date_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sleep_date_label, lv_color_hex(0x9FD8E3), 0);
    lv_label_set_text(sleep_date_label, "----/--/--");
    lv_obj_align(sleep_date_label, LV_ALIGN_TOP_MID, 0, 346);

    lv_timer_create(update_time_cb, 1000, NULL);
    lv_scr_load(scr_firefly);
}

void setup(void) {
    Serial.begin(115200);

#ifdef GFX_EXTRA_PRE_INIT
    GFX_EXTRA_PRE_INIT();
#endif

    gfx->begin();
    gfx_co5300->setBrightness(screen_brightness);
    gfx->fillScreen(BLACK);
    touch_init(gfx->width(), gfx->height(), gfx->getRotation());

    Wire.begin(IIC_SDA, IIC_SCL);
    if(power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        power.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
        power.clearIrqStatus();
        power.enableBattDetection();
        power.enableVbusVoltageMeasure();
        power.enableBattVoltageMeasure();
        power.enableSystemVoltageMeasure();
        power.enableTemperatureMeasure();
    }

    prefs.begin(UI_PREF_NAMESPACE, false);
    load_sound_alarm_preferences();
    init_default_settings_theme();

    if(!rtc.begin(Wire, IIC_SDA, IIC_SCL)) {
        Serial.println("Failed to find PCF85063 RTC");
    } else {
        RTC_DateTime dt = rtc.getDateTime();
        if(dt.getYear() > 2023) {
            sync_time_to_system_from_rtc(dt);
        } else {
            Serial.println("RTC time invalid. Set time manually from Settings.");
        }
    }

    lv_init();

    const size_t preferred_buffer_size = LCD_WIDTH * 120U;
    size_t buffer_size = preferred_buffer_size;
    lv_color_t * buf1 = (lv_color_t *)heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_color_t * buf2 = (lv_color_t *)heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if(!buf1 || !buf2) {
        if(buf1) {
            heap_caps_free(buf1);
            buf1 = NULL;
        }
        if(buf2) {
            heap_caps_free(buf2);
            buf2 = NULL;
        }

        buffer_size = LCD_WIDTH * 80U;
        buf1 = (lv_color_t *)heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        buf2 = (lv_color_t *)heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    if(!buf1 || !buf2) {
        Serial.println("LVGL draw buffer allocation failed.");
        while(true) {
            delay(1000);
        }
    }

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buffer_size);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.rounder_cb = my_rounder_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    build_firefly_os();
    refresh_sound_alarm_ui();
    refresh_battery_ui();
    update_time_cb(NULL);

    pinMode(0, INPUT_PULLUP);
    last_activity_time = millis();
    start_firefly_background_task();
}

void loop() {
    firefly_handle_short_press();
    update_charging_overlay();
    firefly_handle_auto_sleep();

    lv_tick_inc(5);
    lv_timer_handler();
    delay(2);
}
