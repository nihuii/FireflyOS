#include "FireflyApp.h"
#include "touch.h"

SensorPCF85063 rtc;
XPowersPMU power;
Preferences prefs;

const char * UI_PREF_NAMESPACE = "firefly";
const char * UI_PREF_VOLUME_KEY = "ui_volume";
const char * UI_PREF_ALARM_ENABLED_KEY = "alarm_on";
const char * UI_PREF_ALARM_HOUR_KEY = "alarm_hour";
const char * UI_PREF_ALARM_MINUTE_KEY = "alarm_min";

lv_obj_t * lock_date_label = NULL;
lv_obj_t * lock_time_label = NULL;
lv_obj_t * lock_week_label = NULL;
lv_obj_t * notif_panel = NULL;
lv_obj_t * notif_detail_label = NULL;
lv_obj_t * notif_time_label = NULL;
lv_obj_t * top_status_bar = NULL;
lv_obj_t * status_battery_icon = NULL;
lv_obj_t * status_time_label = NULL;
lv_obj_t * notif_volume_slider = NULL;
lv_obj_t * notif_volume_value_label = NULL;
lv_obj_t * notif_brightness_slider = NULL;
lv_obj_t * notif_brightness_value_label = NULL;
lv_obj_t * tile_lock = NULL;
lv_obj_t * tile_sys = NULL;
lv_obj_t * tv_main = NULL;
lv_obj_t * desktop_icon_layer = NULL;
lv_obj_t * settings_panel = NULL;
lv_obj_t * settings_menu_container = NULL;
lv_obj_t * settings_batt_container = NULL;
lv_obj_t * settings_batt_icon = NULL;
lv_obj_t * settings_batt_info = NULL;
lv_obj_t * settings_time_container = NULL;
lv_obj_t * settings_sound_container = NULL;
lv_obj_t * settings_alarm_container = NULL;
lv_obj_t * settings_display_container = NULL;
lv_obj_t * settings_volume_slider = NULL;
lv_obj_t * settings_volume_value_label = NULL;
lv_obj_t * settings_brightness_slider = NULL;
lv_obj_t * settings_brightness_value_label = NULL;
lv_obj_t * settings_sleep_roller = NULL;
lv_obj_t * settings_alarm_summary_label = NULL;
lv_obj_t * settings_alarm_cards[FIREFLY_ALARM_SLOT_COUNT] = {NULL};
lv_obj_t * settings_alarm_time_labels[FIREFLY_ALARM_SLOT_COUNT] = {NULL};
lv_obj_t * settings_alarm_days_labels[FIREFLY_ALARM_SLOT_COUNT] = {NULL};
lv_obj_t * settings_alarm_name_labels[FIREFLY_ALARM_SLOT_COUNT] = {NULL};
lv_obj_t * settings_alarm_empty_labels[FIREFLY_ALARM_SLOT_COUNT] = {NULL};
lv_obj_t * settings_alarm_switches[FIREFLY_ALARM_SLOT_COUNT] = {NULL};
lv_obj_t * settings_alarm_add_button = NULL;
lv_obj_t * settings_alarm_editor = NULL;
lv_obj_t * settings_alarm_editor_title = NULL;
lv_obj_t * settings_alarm_editor_hour_roller = NULL;
lv_obj_t * settings_alarm_editor_minute_roller = NULL;
lv_obj_t * settings_alarm_editor_ringtone_roller = NULL;
lv_obj_t * settings_alarm_editor_days_roller = NULL;
lv_obj_t * settings_alarm_editor_name_ta = NULL;
lv_obj_t * settings_alarm_editor_keyboard = NULL;
lv_obj_t * roller_year = NULL;
lv_obj_t * roller_month = NULL;
lv_obj_t * roller_day = NULL;
lv_obj_t * roller_hour = NULL;
lv_obj_t * roller_minute = NULL;
lv_obj_t * alarm_overlay = NULL;
lv_obj_t * alarm_overlay_title = NULL;
lv_obj_t * alarm_overlay_detail = NULL;
lv_obj_t * charge_overlay = NULL;
lv_obj_t * charge_percent_label = NULL;
lv_obj_t * charge_status_label = NULL;
lv_obj_t * sleep_screen = NULL;
lv_obj_t * sleep_icon_img = NULL;
lv_obj_t * sleep_time_label = NULL;
lv_obj_t * sleep_date_label = NULL;
lv_obj_t * scr_firefly = NULL;

lv_coord_t drag_start_y = 0;
bool is_dragging_notif = false;
volatile bool is_sleeping = false;
volatile bool sleep_display_off = false;
volatile bool is_on_lockscreen = true;

uint8_t screen_brightness = 128;
uint8_t volume_level = 50;
volatile uint32_t auto_sleep_ms = 30000UL;
volatile unsigned long last_activity_time = 0;
volatile unsigned long sleep_entered_at = 0;
volatile unsigned long charge_overlay_started_at = 0;
bool alarm_ringing = false;
volatile bool charging_overlay_visible = false;
bool charging_last_state = false;

lv_color_t settings_theme_accent = LV_COLOR_MAKE(0x6E, 0xC4, 0xD6);
lv_color_t settings_theme_surface = LV_COLOR_MAKE(0x13, 0x1C, 0x2A);
lv_color_t settings_theme_surface_alt = LV_COLOR_MAKE(0x1C, 0x27, 0x38);
lv_color_t settings_theme_text_primary = LV_COLOR_MAKE(0xF4, 0xF8, 0xFC);
lv_color_t settings_theme_text_secondary = LV_COLOR_MAKE(0xA8, 0xB8, 0xCC);
lv_color_t settings_theme_action = LV_COLOR_MAKE(0x8D, 0xE3, 0xF0);

Arduino_DataBus * bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3
);

Arduino_GFX * gfx = new Arduino_CO5300(bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 22, 0, 0, 0);
Arduino_CO5300 * gfx_co5300 = (Arduino_CO5300 *)gfx;

lv_disp_draw_buf_t draw_buf;
