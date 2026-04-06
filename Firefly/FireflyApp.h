#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "LockWallpaper.h"
#include "pin_config.h"
#include "SleepIcons.h"
#include <Wire.h>
#include <Preferences.h>
#include <time.h>
#include "SensorPCF85063.hpp"
#include "XPowersLib.h"

extern "C" {
    extern const lv_font_t lv_font_montserrat_24;
    extern const lv_font_t lv_font_montserrat_48;
}

extern SensorPCF85063 rtc;
extern XPowersPMU power;
extern Preferences prefs;

extern const char * UI_PREF_NAMESPACE;
extern const char * UI_PREF_VOLUME_KEY;
extern const char * UI_PREF_ALARM_ENABLED_KEY;
extern const char * UI_PREF_ALARM_HOUR_KEY;
extern const char * UI_PREF_ALARM_MINUTE_KEY;

extern lv_obj_t * lock_date_label;
extern lv_obj_t * lock_time_label;
extern lv_obj_t * lock_week_label;
extern lv_obj_t * notif_panel;
extern lv_obj_t * notif_detail_label;
extern lv_obj_t * notif_time_label;
extern lv_obj_t * top_status_bar;
extern lv_obj_t * status_battery_icon;
extern lv_obj_t * status_time_label;
extern lv_obj_t * notif_volume_slider;
extern lv_obj_t * notif_volume_value_label;
extern lv_obj_t * notif_brightness_slider;
extern lv_obj_t * notif_brightness_value_label;
extern lv_obj_t * tile_lock;
extern lv_obj_t * tile_sys;
extern lv_obj_t * tv_main;
extern lv_obj_t * desktop_icon_layer;
extern lv_obj_t * settings_panel;
extern lv_obj_t * settings_menu_container;
extern lv_obj_t * settings_batt_container;
extern lv_obj_t * settings_batt_icon;
extern lv_obj_t * settings_batt_info;
extern lv_obj_t * settings_time_container;
extern lv_obj_t * settings_sound_container;
extern lv_obj_t * settings_display_container;
extern lv_obj_t * settings_volume_slider;
extern lv_obj_t * settings_volume_value_label;
extern lv_obj_t * settings_alarm_status_label;
extern lv_obj_t * settings_alarm_time_label;
extern lv_obj_t * settings_alarm_switch;
extern lv_obj_t * settings_brightness_slider;
extern lv_obj_t * settings_brightness_value_label;
extern lv_obj_t * settings_sleep_roller;
extern lv_obj_t * roller_alarm_hour;
extern lv_obj_t * roller_alarm_minute;
extern lv_obj_t * roller_year;
extern lv_obj_t * roller_month;
extern lv_obj_t * roller_day;
extern lv_obj_t * roller_hour;
extern lv_obj_t * roller_minute;
extern lv_obj_t * alarm_overlay;
extern lv_obj_t * alarm_overlay_title;
extern lv_obj_t * alarm_overlay_detail;
extern lv_obj_t * charge_overlay;
extern lv_obj_t * charge_percent_label;
extern lv_obj_t * charge_status_label;
extern lv_obj_t * sleep_screen;
extern lv_obj_t * sleep_icon_img;
extern lv_obj_t * sleep_time_label;
extern lv_obj_t * sleep_date_label;
extern lv_obj_t * scr_firefly;

extern lv_coord_t drag_start_y;
extern bool is_dragging_notif;
extern volatile bool is_sleeping;
extern volatile bool sleep_display_off;
extern volatile bool is_on_lockscreen;

extern uint8_t screen_brightness;
extern uint8_t volume_level;
extern volatile uint32_t auto_sleep_ms;
extern volatile unsigned long last_activity_time;
extern volatile unsigned long sleep_entered_at;
extern volatile unsigned long charge_overlay_started_at;
extern bool alarm_enabled;
extern uint8_t alarm_hour;
extern uint8_t alarm_minute;
extern bool alarm_ringing;
extern String alarm_last_trigger_key;
extern volatile bool charging_overlay_visible;
extern bool charging_last_state;

extern lv_color_t settings_theme_accent;
extern lv_color_t settings_theme_surface;
extern lv_color_t settings_theme_surface_alt;
extern lv_color_t settings_theme_text_primary;
extern lv_color_t settings_theme_text_secondary;
extern lv_color_t settings_theme_action;

extern Arduino_DataBus * bus;
extern Arduino_GFX * gfx;
extern Arduino_CO5300 * gfx_co5300;
extern lv_disp_draw_buf_t draw_buf;

extern int16_t touch_last_x;
extern int16_t touch_last_y;
void touch_init(int16_t w, int16_t h, uint8_t r);
bool touch_touched();
void translate_touch_raw();

void open_settings_panel();
void close_settings_panel();
void set_settings_subpage(lv_obj_t * page);
void sync_time_to_system_from_rtc(const RTC_DateTime& dt);
void refresh_battery_ui();
void refresh_runtime_status_ui();
void load_sound_alarm_preferences();
void save_volume_preference();
void save_alarm_preferences();
void refresh_sound_alarm_ui();
void set_screen_brightness_level(uint8_t brightness);
void dismiss_alarm_alert();
void update_charging_overlay();
void build_firefly_os();
void firefly_handle_short_press();
void firefly_handle_auto_sleep();
void start_firefly_background_task();
void init_default_settings_theme();

void my_disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
void my_rounder_cb(lv_disp_drv_t * disp_drv, lv_area_t * area);
void my_touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);

void tv_event_cb(lv_event_t * e);
void anim_notif_panel_cb(void * var, int32_t v);
void status_drag_cb(lv_event_t * e);
void update_time_cb(lv_timer_t * timer);
void enter_sleep_screen_mode();
void exit_sleep_screen_mode();
