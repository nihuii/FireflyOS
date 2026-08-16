#pragma once

#include <atomic>
#include <Arduino.h>
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "FireflyAlarm.h"
#include "LockWallpaper.h"
#include "SettingsWallpaper.h"
#include "pin_config.h"
#include "SleepIcons.h"
#include <Wire.h>
#include <time.h>
#include "SensorPCF85063.hpp"
#include "XPowersLib.h"
#include <FireflyOS.h>
#include <firefly/hal/LegacyBoardAdapter.h>
#include <firefly/ui/UiComponents.h>

extern "C" {
    extern const lv_font_t lv_font_montserrat_24;
    extern const lv_font_t lv_font_montserrat_48;
}

extern SensorPCF85063 rtc;
extern XPowersPMU power;
extern firefly::I2cBusManager firefly_i2c_bus;
extern firefly::LegacyBoardAdapter firefly_board;
extern firefly::Qmi8658Device qmi8658_device;
extern firefly::SdCardDevice sd_card;
extern firefly::Es8311Device es8311_device;
extern firefly::AudioService audio_service;
extern firefly::AlarmService alarm_service;
extern firefly::PowerService power_service;
extern firefly::WifiService wifi_service;
extern firefly::StorageService storage_service;
extern firefly::LittleFsWeatherCacheStore weather_cache_store;
extern firefly::WeatherService weather_service;
extern firefly::SdBulkTransferStorage bulk_transfer_storage;
extern firefly::EspHttpBulkTransferTransport bulk_transfer_transport;
extern firefly::BulkTransferService bulk_transfer_service;
extern firefly::FileScanService file_scan_service;
extern firefly::ThemePackageService theme_package_service;
extern firefly::SystemSettings system_settings;
extern firefly::TimeService time_service;
extern firefly::MotionService motion_service;
extern firefly::CapabilityRegistry system_capabilities;
extern firefly::HardwareCapabilities hardware_capabilities;
extern firefly::ResourceGovernor system_resources;
extern firefly::SystemLifecycle system_lifecycle;
extern firefly::UpdateService update_service;
extern firefly::SdManifestSource sd_manifest_source;
extern firefly::SdUpdateSource sd_update_source;
extern firefly::HttpsManifestSource https_manifest_source;
extern firefly::HttpsUpdateSource https_update_source;
extern firefly::UpdateCoordinator update_coordinator;
extern firefly::BootValidationService boot_validation_service;
extern firefly::DiagnosticService diagnostic_service;
extern firefly::FactoryResetService factory_reset_service;
extern firefly::SerialDiagnosticExport serial_diagnostic_export;
extern firefly::SdDiagnosticExport sd_diagnostic_export;
extern firefly::UiShell ui_shell;
extern firefly::GlanceScreen glance_screen;
extern firefly::LockScreen lock_screen;
extern firefly::HomeScreen home_screen;
extern firefly::ActivityApp activity_app;
extern firefly::WeatherApp weather_app;
extern firefly::UpdateApp update_app;
extern firefly::CalendarApp calendar_app;
extern firefly::ClockApp clock_app;
extern firefly::SettingsApp settings_app;
extern firefly::ToolsApp tools_app;
extern firefly::FilesApp files_app;
extern firefly::MusicApp music_app;
extern firefly::RecorderApp recorder_app;
extern firefly::ThemesApp themes_app;
extern firefly::AppShellScreen app_shell_screen;
extern firefly::AppRegistry ui_app_registry;
extern firefly::ControlCenter control_center;
extern firefly::NotificationCenter notification_center;
extern firefly::NotificationService notification_service;
extern firefly::CompanionSyncService companion_sync_service;
extern firefly::CompanionFrameDispatcher companion_frame_dispatcher;
extern firefly::StateStore ui_state_store;
extern firefly::PairingOverlay pairing_overlay;
extern firefly::WifiProvisionOverlay wifi_provision_overlay;
extern firefly::BlePeripheralDevice ble_peripheral_device;
extern firefly::ConnectivityService connectivity_service;

extern lv_obj_t * lock_date_label;
extern lv_obj_t * lock_time_label;
extern lv_obj_t * lock_week_label;
extern lv_obj_t * notif_panel;
extern lv_obj_t * notif_detail_label;
extern lv_obj_t * notif_time_label;
extern lv_obj_t * top_status_bar;
extern lv_obj_t * status_battery_icon;
extern lv_obj_t * status_time_label;
extern lv_obj_t * media_status_label;
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
extern lv_obj_t * settings_alarm_container;
extern lv_obj_t * settings_display_container;
extern lv_obj_t * settings_reset_container;
extern lv_obj_t * settings_reset_title;
extern lv_obj_t * settings_reset_detail;
extern lv_obj_t * settings_reset_notice;
extern lv_obj_t * settings_reset_keep_button;
extern lv_obj_t * settings_reset_sd_button;
extern lv_obj_t * settings_reset_cancel_button;
extern lv_obj_t * settings_volume_slider;
extern lv_obj_t * settings_volume_value_label;
extern lv_obj_t * settings_brightness_slider;
extern lv_obj_t * settings_brightness_value_label;
extern lv_obj_t * settings_sleep_roller;
extern lv_obj_t * settings_alarm_summary_label;
extern lv_obj_t * settings_alarm_cards[FIREFLY_ALARM_SLOT_COUNT];
extern lv_obj_t * settings_alarm_time_labels[FIREFLY_ALARM_SLOT_COUNT];
extern lv_obj_t * settings_alarm_days_labels[FIREFLY_ALARM_SLOT_COUNT];
extern lv_obj_t * settings_alarm_name_labels[FIREFLY_ALARM_SLOT_COUNT];
extern lv_obj_t * settings_alarm_empty_labels[FIREFLY_ALARM_SLOT_COUNT];
extern lv_obj_t * settings_alarm_switches[FIREFLY_ALARM_SLOT_COUNT];
extern lv_obj_t * settings_alarm_add_button;
extern lv_obj_t * settings_alarm_editor;
extern lv_obj_t * settings_alarm_editor_title;
extern lv_obj_t * settings_alarm_editor_hour_roller;
extern lv_obj_t * settings_alarm_editor_minute_roller;
extern lv_obj_t * settings_alarm_editor_ringtone_roller;
extern lv_obj_t * settings_alarm_editor_days_roller;
extern lv_obj_t * settings_alarm_editor_name_ta;
extern lv_obj_t * settings_alarm_editor_keyboard;
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
extern lv_obj_t * sleep_media_status_label;
extern lv_obj_t * scr_firefly;

extern lv_coord_t drag_start_y;
extern bool is_dragging_notif;
extern volatile bool is_sleeping;
extern volatile bool sleep_display_off;
extern volatile bool is_on_lockscreen;
extern volatile bool activity_app_active;

extern uint8_t screen_brightness;
extern uint8_t volume_level;
extern volatile uint32_t auto_sleep_ms;
extern volatile unsigned long last_activity_time;
extern volatile unsigned long sleep_entered_at;
extern volatile unsigned long charge_overlay_started_at;
extern volatile uint32_t settings_command_failures;
extern std::atomic<bool> alarm_ringing;
extern std::atomic<int8_t> factory_reset_execute_request;
extern std::atomic<bool> factory_reset_reboot_pending;
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
bool touch_ready();
void translate_touch_raw();

void open_settings_panel();
void close_settings_panel();
void set_settings_subpage(lv_obj_t * page);
void sync_time_to_system_from_epoch(int64_t epoch_seconds);
void refresh_battery_ui();
void refresh_runtime_status_ui();
void load_sound_alarm_preferences();
void load_companion_settings_preferences();
void save_volume_preference();
bool firefly_apply_local_music_volume(uint8_t volume);
void save_alarm_preferences();
void load_motion_summary_preference();
void persist_motion_summary(bool force);
void refresh_sound_alarm_ui();
void clear_alarm_trigger_history();
void set_screen_brightness_level(uint8_t brightness);
void dismiss_alarm_alert();
void update_charging_overlay();
void build_firefly_os();
void firefly_process_system_events();
void firefly_process_sd_card();
void firefly_process_clock_sessions();
void firefly_process_settings_commands();
void firefly_process_power_policy();
void firefly_process_tools_commands();
void firefly_process_media_apps();
void firefly_process_factory_reset();
bool firefly_factory_reset_active();
void firefly_refresh_companion_weather_ui();
void firefly_process_connectivity();
void firefly_report_gate_a_diagnostics();
void firefly_sample_diagnostics();
void firefly_export_diagnostics();
void start_firefly_background_task();
firefly::UpdateRuntimeGate firefly_current_update_gate();
bool firefly_audio_start_allowed(firefly::AudioUse use, void * context);
void configure_power_sleep_hooks();
void init_default_settings_theme();
void init_settings_theme_from_wallpaper(const lv_img_dsc_t * wallpaper);
bool firefly_send_phone_media_command(
    firefly::RemoteMediaCommand command,
    uint8_t volume_percent);
bool firefly_send_find_phone_command();

void my_disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
void my_rounder_cb(lv_disp_drv_t * disp_drv, lv_area_t * area);
void my_touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);

void tv_event_cb(lv_event_t * e);
void anim_notif_panel_cb(void * var, int32_t v);
void status_drag_cb(lv_event_t * e);
void update_time_cb(lv_timer_t * timer);
void enter_sleep_screen_mode();
void exit_sleep_screen_mode();
