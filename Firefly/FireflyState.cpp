#include "FireflyApp.h"
#include <SD_MMC.h>
#include "touch.h"

SensorPCF85063 rtc;
XPowersPMU power;

lv_obj_t * lock_date_label = NULL;
lv_obj_t * lock_time_label = NULL;
lv_obj_t * lock_week_label = NULL;
lv_obj_t * notif_panel = NULL;
lv_obj_t * notif_detail_label = NULL;
lv_obj_t * notif_time_label = NULL;
lv_obj_t * top_status_bar = NULL;
lv_obj_t * status_battery_icon = NULL;
lv_obj_t * status_time_label = NULL;
lv_obj_t * media_status_label = NULL;
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
lv_obj_t * settings_reset_container = NULL;
lv_obj_t * settings_reset_title = NULL;
lv_obj_t * settings_reset_detail = NULL;
lv_obj_t * settings_reset_notice = NULL;
lv_obj_t * settings_reset_keep_button = NULL;
lv_obj_t * settings_reset_sd_button = NULL;
lv_obj_t * settings_reset_cancel_button = NULL;
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
lv_obj_t * sleep_media_status_label = NULL;
lv_obj_t * scr_firefly = NULL;

lv_coord_t drag_start_y = 0;
bool is_dragging_notif = false;
volatile bool is_sleeping = false;
volatile bool sleep_display_off = false;
volatile bool is_on_lockscreen = true;
volatile bool activity_app_active = false;

uint8_t screen_brightness = 128;
uint8_t volume_level = 50;
volatile uint32_t auto_sleep_ms = 30000UL;
volatile unsigned long last_activity_time = 0;
volatile unsigned long sleep_entered_at = 0;
volatile unsigned long charge_overlay_started_at = 0;
volatile uint32_t settings_command_failures = 0;
std::atomic<bool> alarm_ringing{false};
std::atomic<int8_t> factory_reset_execute_request{-1};
std::atomic<bool> factory_reset_reboot_pending{false};
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

firefly::I2cBusManager firefly_i2c_bus(Wire);
firefly::LegacyBoardAdapter firefly_board(rtc, power, *gfx_co5300, &firefly_i2c_bus);
firefly::Qmi8658Device qmi8658_device(firefly_i2c_bus);
firefly::SdCardDevice sd_card;
firefly::Es8311Device es8311_device(firefly_i2c_bus);
firefly::AudioService audio_service(es8311_device);
firefly::AlarmService alarm_service;
firefly::PowerService power_service;
firefly::WifiService wifi_service;
firefly::StorageService storage_service;
firefly::LittleFsWeatherCacheStore weather_cache_store;
firefly::WeatherService weather_service(weather_cache_store, wifi_service);
firefly::SdBulkTransferStorage bulk_transfer_storage(storage_service);
firefly::EspHttpBulkTransferTransport bulk_transfer_transport;
firefly::BulkTransferService bulk_transfer_service(
    bulk_transfer_storage, bulk_transfer_transport, power_service, wifi_service);
firefly::FileScanService file_scan_service;
firefly::ThemePackageService theme_package_service;
firefly::SystemSettings system_settings;
firefly::TimeService time_service(firefly_board);
firefly::MotionService motion_service(qmi8658_device);
firefly::CapabilityRegistry system_capabilities;
firefly::HardwareCapabilities hardware_capabilities;
firefly::ResourceGovernor system_resources;
firefly::SystemLifecycle system_lifecycle;
firefly::EspOtaWriter ota_writer;
firefly::UpdateService update_service(
    system_resources, ota_writer, FIREFLYOS_BUILD);
firefly::SdManifestSource sd_manifest_source(
    storage_service, "/FireflyOS/Updates/update.json");
firefly::SdUpdateSource sd_update_source(
    storage_service, "/FireflyOS/Updates/update.bin");
firefly::HttpsManifestSource https_manifest_source;
firefly::HttpsUpdateSource https_update_source;
firefly::UpdateCoordinator update_coordinator(
    update_service, wifi_service,
    sd_manifest_source, sd_update_source,
    https_manifest_source, https_update_source);
firefly::EspOtaBootPlatform ota_boot_platform;
firefly::BootValidationService boot_validation_service(ota_boot_platform);
firefly::DiagnosticService diagnostic_service;
firefly::SerialDiagnosticExport serial_diagnostic_export;
firefly::SdDiagnosticExport sd_diagnostic_export(storage_service);
firefly::UiShell ui_shell;
firefly::GlanceScreen glance_screen;
firefly::LockScreen lock_screen;
firefly::HomeScreen home_screen;
firefly::ActivityApp activity_app;
firefly::WeatherApp weather_app;
firefly::UpdateApp update_app;
firefly::CalendarApp calendar_app;
firefly::ClockApp clock_app;
firefly::SettingsApp settings_app;
firefly::ToolsApp tools_app;
firefly::FilesApp files_app;
firefly::MusicApp music_app;
firefly::RecorderApp recorder_app;
firefly::ThemesApp themes_app;
firefly::AppShellScreen app_shell_screen;
firefly::AppRegistry ui_app_registry;
firefly::ControlCenter control_center;
firefly::NotificationCenter notification_center;
firefly::NotificationService notification_service;
firefly::StateStore ui_state_store;
firefly::PairingOverlay pairing_overlay;
firefly::WifiProvisionOverlay wifi_provision_overlay;

lv_disp_draw_buf_t draw_buf;
