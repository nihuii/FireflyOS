#include "FireflyApp.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <sys/time.h>

firefly::EventBus system_event_bus;
firefly::BlePeripheralDevice ble_peripheral_device;
firefly::ConnectivityService connectivity_service(
    ble_peripheral_device, system_event_bus, ui_state_store, storage_service
);

class FireflyFactoryResetOwners final : public firefly::FactoryResetOwners {
public:
    bool clearPairing() override {
        return connectivity_service.clearSensitiveState();
    }
    bool clearWifi() override { return wifi_service.clearSensitiveState(); }
    bool clearNotifications() override {
        return notification_service.clearSensitiveState();
    }
    bool clearWeather() override {
        return weather_service.clearSensitiveState();
    }
    bool clearSettings() override {
        return storage_service.clearInternalUserData();
    }
    bool clearCaches() override { return storage_service.clearThemeCache(); }
    bool clearManagedSdRoot() override {
        return storage_service.clearManagedSdRoot();
    }
};

class FireflyFactoryResetRebooter final
    : public firefly::FactoryResetRebooter {
public:
    bool requestReboot() override {
        factory_reset_reboot_pending.store(true, std::memory_order_release);
        return true;
    }
};

FireflyFactoryResetOwners factory_reset_owners;
FireflyFactoryResetRebooter factory_reset_rebooter;
firefly::FactoryResetService factory_reset_service(
    factory_reset_owners, factory_reset_rebooter);

class FireflyCompanionSettingsPersistence final
    : public firefly::CompanionSettingsPersistence {
public:
    bool saveSnapshot(
        const firefly::CompanionSettingsSnapshot & snapshot) override;
    bool restoreSnapshot(
        const firefly::CompanionSettingsSnapshot & snapshot);
};

FireflyCompanionSettingsPersistence companion_settings_persistence;
firefly::CompanionSyncService companion_sync_service(
    &companion_settings_persistence
);
firefly::CompanionFrameDispatcher companion_frame_dispatcher(
    notification_service, companion_sync_service
);

namespace {

TaskHandle_t firefly_background_task_handle = NULL;
TaskHandle_t firefly_weather_task_handle = NULL;
TaskHandle_t firefly_bulk_task_handle = NULL;
TaskHandle_t firefly_update_task_handle = NULL;
bool firefly_background_task_running = false;
bool firefly_weather_task_running = false;
bool firefly_bulk_task_running = false;
bool firefly_update_task_running = false;
volatile uint32_t event_post_failures = 0;
uint32_t desktop_transition_released_at = 0;
uint32_t desktop_transition_max_ms = 0;
volatile bool power_menu_visible = false;
bool motion_summary_preference_loaded = false;
uint32_t motion_last_saved_at = 0;
uint32_t motion_last_saved_day = 0;
uint32_t motion_last_saved_steps = UINT32_MAX;
uint16_t motion_last_saved_active_minutes = UINT16_MAX;
volatile firefly::PowerMode runtime_power_mode = firefly::PowerMode::Active;
bool light_sleep_entered = false;
bool light_sleep_motion_low_power = false;
bool find_watch_feedback_visible = false;
bool find_watch_feedback_sound_started = false;
uint8_t find_watch_feedback_restore_brightness = 0;
char companion_error_status[16]{};
uint32_t companion_error_status_until = 0;
bool ntp_request_configured = false;
bool ntp_retry_after_alarm = false;
bool sd_removal_cleanup_pending = false;
constexpr uint32_t kFactoryResetTaskStackWords = 4096;
StaticTask_t factory_reset_task_storage{};
StackType_t factory_reset_task_stack[kFactoryResetTaskStackWords]{};
TaskHandle_t factory_reset_task_handle = NULL;
std::atomic<bool> factory_reset_worker_active{false};
std::atomic<bool> factory_reset_worker_finished{false};
std::atomic<bool> factory_reset_worker_completed{false};
std::atomic<bool> factory_reset_worker_erase_sd{false};
constexpr uint32_t kUpdateTaskStackWords = 8192;
StaticTask_t update_task_storage{};
StackType_t update_task_stack[kUpdateTaskStackWords]{};

void stop_network_time_request() {
    if(ntp_request_configured) esp_sntp_stop();
    ntp_request_configured = false;
}

void requestSdRemovalCleanup() {
    const firefly::BulkTransferState state =
        bulk_transfer_service.snapshot().state;
    if(state == firefly::BulkTransferState::WaitingForNetwork ||
       state == firefly::BulkTransferState::Ready ||
       state == firefly::BulkTransferState::Receiving ||
       state == firefly::BulkTransferState::Completed) {
        bulk_transfer_service.cancel(
            firefly::BulkTransferFailure::SdUnavailable, millis());
    }
    sd_removal_cleanup_pending = true;
}

void finishSdRemovalCleanup() {
    if(!sd_removal_cleanup_pending ||
       storage_service.bulkSdSessionActive()) return;
    if(audio_service.activeUse() == firefly::AudioUse::Music ||
       audio_service.activeUse() == firefly::AudioUse::Recorder) {
        audio_service.stop();
    }
    files_app.onSdRemoved();
    music_app.onSdRemoved();
    recorder_app.onSdRemoved();
    themes_app.onSdRemoved();
    storage_service.detachSd();
    sd_removal_cleanup_pending = false;
    Serial.println("SD card unavailable; media features disabled.");
}

void service_network_time(uint32_t now_ms) {
    const bool alarm_is_ringing =
        alarm_ringing.load(std::memory_order_acquire);
    if(alarm_is_ringing &&
       wifi_service.active(firefly::WifiPurpose::Ntp)) {
        ntp_retry_after_alarm = true;
        stop_network_time_request();
        wifi_service.release(firefly::WifiPurpose::Ntp, now_ms);
        return;
    }
    if(ntp_retry_after_alarm && !alarm_is_ringing &&
       !wifi_service.active(firefly::WifiPurpose::Ntp)) {
        if(!wifi_service.request(firefly::WifiPurpose::Ntp, now_ms)) return;
        ntp_retry_after_alarm = false;
    }
    if(!wifi_service.active(firefly::WifiPurpose::Ntp)) {
        stop_network_time_request();
        time_service.flushDeferredNetworkTime(alarm_is_ringing);
        return;
    }
    if(wifi_service.mode() != firefly::WifiMode::Connected) return;
    if(!ntp_request_configured) {
        configTzTime("CST-8", "pool.ntp.org", "time.cloudflare.com");
        ntp_request_configured = true;
        return;
    }
    if(sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) return;
    const int64_t network_epoch = static_cast<int64_t>(time(nullptr));
    const bool applied = network_epoch > 1609459200LL &&
        time_service.applyNetworkTime(network_epoch, alarm_is_ringing);
    stop_network_time_request();
    wifi_service.release(firefly::WifiPurpose::Ntp, now_ms);
    if(!applied) time_service.flushDeferredNetworkTime(alarm_is_ringing);
}

void refresh_notification_center_from_service() {
    firefly::NotificationSummary visible[
        firefly::NotificationCenter::kVisibleLimit
    ]{};
    const uint8_t available = notification_service.count();
    const uint8_t count = available < firefly::NotificationCenter::kVisibleLimit
        ? available
        : firefly::NotificationCenter::kVisibleLimit;
    for(uint8_t index = 0; index < count; ++index) {
        notification_service.copyForDisplay(
            index, is_on_lockscreen, visible[index]
        );
    }
    notification_center.setNotifications(visible, count);
}

bool decode_companion_alarm(const firefly::VersionedCompanionSetting & setting,
                            uint8_t & slot,
                            firefly::Alarm & alarm) {
    return firefly::CompanionSyncService::decodeAlarm(setting, slot, alarm);
}

bool resolve_remote_theme_palette(const char * theme_id,
                                  uint32_t palette[5]) {
    if(!theme_id || !theme_id[0] || !palette) return false;
    char cached_theme_id[sizeof(system_settings.theme_id)]{};
    uint32_t cached_palette[5]{};
    bool cache_present = false;
    if(storage_service.loadThemeCache(
           cached_theme_id, sizeof(cached_theme_id),
           cached_palette, cache_present) &&
       cache_present && strcmp(theme_id, cached_theme_id) == 0) {
        memcpy(palette, cached_palette, sizeof(cached_palette));
        return true;
    }
    const firefly::ThemeManifest & firefly_default =
        firefly::ThemePackageService::fireflyDefault();
    if(strcmp(theme_id, firefly_default.id) == 0) {
        memcpy(palette, firefly_default.palette,
               sizeof(firefly_default.palette));
        return true;
    }
    const firefly::ThemeManifest & neutral_default =
        firefly::ThemePackageService::neutralDefault();
    if(strcmp(theme_id, neutral_default.id) == 0) {
        memcpy(palette, neutral_default.palette,
               sizeof(neutral_default.palette));
        return true;
    }

    return false;
}

const char * normalize_theme_id(const char * theme_id) {
    return theme_id && strcmp(theme_id, "firefly-default") == 0
        ? "system-default" : theme_id;
}

void apply_runtime_theme_palette(const uint32_t palette[5]) {
    if(!palette) return;
    const firefly::UiTokens previous_tokens =
        firefly::UiTheme::fireflyDefault();
    const firefly::UiTokens next_tokens =
        firefly::UiTheme::fromPalette(palette);
    firefly::UiComponents::applyThemeTree(
        scr_firefly, previous_tokens, next_tokens);
    firefly::UiTheme::setRuntime(next_tokens);
    settings_theme_surface = lv_color_hex(palette[1]);
    settings_theme_surface_alt = lv_color_hex(palette[3]);
    settings_theme_accent = lv_color_hex(palette[2]);
    settings_theme_action = lv_color_hex(palette[3]);
    if(scr_firefly) lv_obj_invalidate(scr_firefly);
}

bool prepare_companion_settings_snapshot(
    const firefly::CompanionSettingsSnapshot & snapshot,
    firefly::SystemSettings & next,
    bool & has_alarm,
    uint8_t & alarm_slot,
    firefly::Alarm & alarm) {
    if(snapshot.schema_version !=
       firefly::CompanionSettingsSnapshot::kSchemaVersion) {
        return false;
    }
    next = system_settings;
    has_alarm = false;
    for(uint8_t index = 0;
        index < firefly::CompanionSettingsSnapshot::kCapacity;
        ++index) {
        if(snapshot.valid[index] > 1) return false;
        if(!snapshot.valid[index]) continue;
        const firefly::VersionedCompanionSetting & setting =
            snapshot.settings[index];
        const firefly::CompanionSettingKind kind =
            static_cast<firefly::CompanionSettingKind>(index + 1);
        if(kind == firefly::CompanionSettingKind::Alarm) {
            if(!decode_companion_alarm(setting, alarm_slot, alarm)) {
                return false;
            }
            has_alarm = true;
        } else if(kind == firefly::CompanionSettingKind::Brightness) {
            if(setting.value_length != 1 || setting.value[0] < 20) {
                return false;
            }
            next.brightness = setting.value[0];
        } else if(kind == firefly::CompanionSettingKind::Volume) {
            if(setting.value_length != 1 || setting.value[0] > 100) {
                return false;
            }
            next.volume = setting.value[0];
        } else if(kind == firefly::CompanionSettingKind::Theme) {
            if(setting.value_length == 0 ||
               setting.value_length >= sizeof(next.theme_id)) {
                return false;
            }
            memcpy(next.theme_id, setting.value, setting.value_length);
            next.theme_id[setting.value_length] = '\0';
            const char * normalized = normalize_theme_id(next.theme_id);
            if(normalized != next.theme_id) {
                strlcpy(next.theme_id, normalized, sizeof(next.theme_id));
            }
        } else {
            return false;
        }
    }
    return true;
}

void update_companion_calendar_ui() {
    const firefly::CompanionCalendar & synced =
        companion_sync_service.calendar();
    firefly::CalendarAgendaCache agenda;
    firefly::CalendarSummary summaries[
        firefly::CalendarAgendaCache::kMaxSummaries
    ]{};
    for(uint8_t index = 0; index < synced.count; ++index) {
        summaries[index].valid = true;
        summaries[index].start_epoch =
            synced.entries[index].start_epoch_ms / 1000;
        strlcpy(summaries[index].title, synced.entries[index].title,
                sizeof(summaries[index].title));
    }
    agenda.setSummaries(summaries, synced.enabled ? synced.count : 0,
                        synced.updated_at_epoch_ms / 1000);
    calendar_app.setAgenda(agenda);
}

void stop_find_watch_feedback() {
    if(find_watch_feedback_sound_started &&
       audio_service.activeUse() == firefly::AudioUse::System) {
        audio_service.stop();
    }
    find_watch_feedback_sound_started = false;
    if(find_watch_feedback_visible) {
        firefly_board.setDisplayBrightness(
            find_watch_feedback_restore_brightness
        );
    }
    find_watch_feedback_visible = false;
}

bool cancel_find_watch_feedback() {
    if(!companion_sync_service.cancelFindWatch()) return false;
    stop_find_watch_feedback();
    return true;
}

void service_find_watch_feedback(uint32_t now_ms) {
    const firefly::FindWatchState state =
        companion_sync_service.findWatchAt(now_ms);
    if(!state.active) {
        stop_find_watch_feedback();
        return;
    }
    if(!find_watch_feedback_visible) {
        find_watch_feedback_visible = true;
        find_watch_feedback_restore_brightness = screen_brightness;
        if(state.play_audio &&
           audio_service.activeUse() == firefly::AudioUse::None &&
           system_capabilities.has(firefly::Capability::Audio)) {
            const firefly::AlarmToneResource & tone =
                firefly::AlarmService::ringtoneResource(0);
            find_watch_feedback_sound_started =
                audio_service.startLoopingPcm(
                    tone.samples, tone.frames, tone.sample_rate,
                    firefly::AudioUse::System
                );
        }
    }
    const bool bright_phase = ((now_ms / 500U) & 1U) == 0;
    firefly_board.setDisplayBrightness(bright_phase ? 255 : 20);
}

uint32_t current_local_day_key() {
    const time_t current_time = time(nullptr);
    struct tm local{};
    if(current_time <= 0 || !localtime_r(&current_time, &local) ||
       local.tm_year + 1900 < 2024) {
        return 0;
    }
    return static_cast<uint32_t>(local.tm_year + 1900) * 1000UL +
           static_cast<uint32_t>(local.tm_yday + 1);
}

void post_background_system_event(const firefly::SystemEvent & event) {
    if(!system_event_bus.post(event)) {
        ++event_post_failures;
    }
}

firefly::ButtonAction poll_boot_button(uint32_t now_ms) {
    static firefly::DebouncedButton boot_button;
    return boot_button.update(digitalRead(0) == LOW, now_ms);
}

firefly::PowerButtonEvent poll_power_button(uint32_t now_ms) {
    static uint32_t last_poll_at = 0;
    if(!system_capabilities.has(firefly::Capability::PowerButton) ||
       now_ms - last_poll_at < 50) {
        return firefly::PowerButtonEvent::None;
    }
    last_poll_at = now_ms;
    return firefly_board.readPowerButtonEvent();
}

bool poll_motion_source(uint32_t now_ms) {
    if(!system_capabilities.has(firefly::Capability::Motion)) return false;

    static uint32_t last_day_check_at = 0;
    if(last_day_check_at == 0 || now_ms - last_day_check_at >= 60000UL) {
        last_day_check_at = now_ms;
        motion_service.setDayKey(current_local_day_key());
    }

    const firefly::SystemState state = ui_state_store.snapshot();
    firefly::MotionContext context{};
    context.screen_on = !state.screen_off;
    context.charging = state.battery.charging || state.battery.vbus_present;
    context.high_rate_app = activity_app_active;
    motion_service.poll(context);
    return motion_service.consumeWristRaise();
}

bool prepare_verified_light_sleep() {
    light_sleep_entered = false;
    const firefly::UpdateState update_state = update_service.snapshot().state;
    if(update_state == firefly::UpdateState::Available ||
       update_state == firefly::UpdateState::Downloading ||
       update_state == firefly::UpdateState::Verifying ||
       update_state == firefly::UpdateState::Writing ||
       update_state == firefly::UpdateState::RebootPending) {
        return false;
    }
    persist_motion_summary(true);
    tools_app.closeFlashlightFromInput();
    audio_service.stop();
    if(firefly_background_task_handle) {
        vTaskSuspend(firefly_background_task_handle);
    }
    if(firefly_weather_task_handle) vTaskSuspend(firefly_weather_task_handle);
    if(firefly_bulk_task_handle) vTaskSuspend(firefly_bulk_task_handle);
    if(firefly_update_task_handle) vTaskSuspend(firefly_update_task_handle);
    if(system_capabilities.has(firefly::Capability::Motion)) {
        const firefly::MotionPowerMode motion_mode =
            firefly::MotionPowerPolicy::modeFor(true, true);
        light_sleep_motion_low_power =
            motion_mode == firefly::MotionPowerMode::LowPower &&
            motion_service.setLowPower(true);
        if(!light_sleep_motion_low_power) {
            if(firefly_background_task_handle) {
                vTaskResume(firefly_background_task_handle);
            }
            if(firefly_weather_task_handle) vTaskResume(firefly_weather_task_handle);
            if(firefly_bulk_task_handle) vTaskResume(firefly_bulk_task_handle);
            if(firefly_update_task_handle) vTaskResume(firefly_update_task_handle);
            return false;
        }
    }
    firefly_board.setDisplayBrightness(0);
    return true;
}

bool enter_verified_light_sleep() {
#if defined(FIREFLY_PWR_WAKE_GPIO) && defined(FIREFLY_RTC_WAKE_GPIO)
    const uint64_t wake_mask =
        (1ULL << 0) |
        (1ULL << FIREFLY_PWR_WAKE_GPIO) |
        (1ULL << FIREFLY_RTC_WAKE_GPIO);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if(esp_sleep_enable_ext1_wakeup(wake_mask,
                                    ESP_EXT1_WAKEUP_ANY_LOW) != ESP_OK) {
        return false;
    }
    light_sleep_entered = esp_light_sleep_start() == ESP_OK;
    return light_sleep_entered;
#else
    return false;
#endif
}

void restore_verified_light_sleep() {
    if(light_sleep_motion_low_power) {
        if(!motion_service.setLowPower(false)) {
            system_capabilities.set(firefly::Capability::Motion, false);
            hardware_capabilities.set(
                firefly::HardwareDevice::Imu,
                firefly::HardwareAvailability::Unavailable,
                firefly::HardwareFailure::IoFailure);
        }
        light_sleep_motion_low_power = false;
    }
    if(firefly_background_task_handle) {
        vTaskResume(firefly_background_task_handle);
    }
    if(firefly_weather_task_handle) vTaskResume(firefly_weather_task_handle);
    if(firefly_bulk_task_handle) vTaskResume(firefly_bulk_task_handle);
    if(firefly_update_task_handle) vTaskResume(firefly_update_task_handle);
    if(light_sleep_entered) {
        sleep_display_off = false;
        sleep_entered_at = millis();
        ui_state_store.setSleepState(true, false);
        glance_screen.show();
        ui_shell.bringAppToFront(sleep_screen);
        firefly_board.setDisplayBrightness(screen_brightness);
    }
    light_sleep_entered = false;
}

firefly::PowerMode evaluate_runtime_power_mode(unsigned long now) {
    const unsigned long last_activity = last_activity_time;
    const uint32_t auto_sleep = auto_sleep_ms;
    const firefly::SystemState state = ui_state_store.snapshot();
    power_service.setBatteryState(state.battery);

    if(power_menu_visible) {
        runtime_power_mode = firefly::PowerMode::Active;
        return firefly::PowerMode::Active;
    }
    if(is_sleeping) {
        const unsigned long entered_at = sleep_entered_at;
        if(entered_at == 0) return firefly::PowerMode::Glance;
        power_service.configure({0, 0, 2000});
        power_service.onActivity(entered_at);
    } else if(!is_on_lockscreen || auto_sleep == 0 || last_activity == 0) {
        power_service.configure({UINT32_MAX, 0, 0});
        power_service.onActivity(now);
    } else {
        power_service.configure({auto_sleep, 0, 2000});
        power_service.onActivity(last_activity);
    }

    const firefly::PowerMode evaluated = power_service.evaluate(now);
    runtime_power_mode = evaluated;
    if(evaluated == firefly::PowerMode::Charging ||
       evaluated == firefly::PowerMode::ThermalProtection) {
        return firefly::PowerMode::Active;
    }
    if(evaluated == firefly::PowerMode::Saver ||
       evaluated == firefly::PowerMode::LowBattery ||
       evaluated == firefly::PowerMode::CriticalBattery) {
        return power_service.evaluateIdle(now);
    }
    return evaluated;
}

void apply_sleep_blackout() {
    if(!is_sleeping || sleep_display_off) {
        return;
    }
    if(recorder_app.recording()) {
        return;
    }

    sleep_display_off = true;
    ui_state_store.setSleepState(true, true);
    firefly_board.setDisplayBrightness(0);
    glance_screen.hide();
    power_service.attemptLightSleep(enter_verified_light_sleep);
}

void wake_sleep_screen_from_blackout();

lv_obj_t * power_menu_overlay = nullptr;

void close_power_menu() {
    power_menu_visible = false;
    if(power_menu_overlay) ui_shell.closeOverlay(power_menu_overlay);
}

void power_menu_cancel_cb(lv_event_t * event) {
    LV_UNUSED(event);
    close_power_menu();
}

void power_menu_sleep_cb(lv_event_t * event) {
    LV_UNUSED(event);
    close_power_menu();
    if(is_sleeping) {
        apply_sleep_blackout();
    } else {
        enter_sleep_screen_mode();
    }
}

void power_menu_restart_cb(lv_event_t * event) {
    LV_UNUSED(event);
    ESP.restart();
}

void power_menu_shutdown_cb(lv_event_t * event) {
    LV_UNUSED(event);
    recorder_app.stopForSafety();
    firefly_board.setDisplayBrightness(0);
    firefly_board.shutdown();
}

lv_obj_t * create_power_menu_button(lv_obj_t * parent,
                                    const char * text,
                                    int16_t y,
                                    lv_event_cb_t callback,
                                    uint32_t color) {
    lv_obj_t * button = lv_btn_create(parent);
    lv_obj_set_size(button, 330, 56);
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(button, 20, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    lv_obj_t * label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xEFFFFB), 0);
    lv_obj_center(label);
    return button;
}

void ensure_power_menu() {
    if(power_menu_overlay || !ui_shell.overlayHost()) return;
    power_menu_overlay = lv_obj_create(ui_shell.overlayHost());
    lv_obj_set_size(power_menu_overlay, 410, 502);
    lv_obj_set_style_bg_color(power_menu_overlay, lv_color_hex(0x020607), 0);
    lv_obj_set_style_bg_opa(power_menu_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(power_menu_overlay, 0, 0);
    lv_obj_set_style_radius(power_menu_overlay, 0, 0);
    lv_obj_clear_flag(power_menu_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(power_menu_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * title = lv_label_create(power_menu_overlay);
    lv_label_set_text(title, "Power menu");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xEFFFFB), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 64);

    lv_obj_t * detail = lv_label_create(power_menu_overlay);
    lv_label_set_text(detail, "Hold PWR | Choose an action");
    lv_obj_set_style_text_color(detail, lv_color_hex(0x8BA6AA), 0);
    lv_obj_align(detail, LV_ALIGN_TOP_MID, 0, 102);

    create_power_menu_button(power_menu_overlay, "Sleep", 152,
                             power_menu_sleep_cb, 0x153238);
    create_power_menu_button(power_menu_overlay, "Restart", 220,
                             power_menu_restart_cb, 0x244149);
    create_power_menu_button(power_menu_overlay, "Shutdown", 288,
                             power_menu_shutdown_cb, 0x5A252B);
    create_power_menu_button(power_menu_overlay, "Cancel", 384,
                             power_menu_cancel_cb, 0x162126);
}

void show_power_menu() {
    ensure_power_menu();
    if(power_menu_overlay) {
        power_menu_visible = true;
        ui_shell.showOverlay(5, power_menu_overlay);
    }
}

void run_power_press_action(firefly::ButtonAction action) {
    if(action == firefly::ButtonAction::None) return;
    if(cancel_find_watch_feedback()) return;
    if(tools_app.closeFlashlightFromInput()) return;
    if(action == firefly::ButtonAction::LongPress) {
        if(is_sleeping) exit_sleep_screen_mode();
        show_power_menu();
        return;
    }
    if(power_menu_overlay &&
       !lv_obj_has_flag(power_menu_overlay, LV_OBJ_FLAG_HIDDEN)) {
        close_power_menu();
    } else if(alarm_ringing) {
        dismiss_alarm_alert();
    } else if(is_sleeping && sleep_display_off) {
        wake_sleep_screen_from_blackout();
    } else if(is_sleeping) {
        apply_sleep_blackout();
    } else {
        enter_sleep_screen_mode();
    }
}

void run_short_press_action() {
    if(cancel_find_watch_feedback()) {
        return;
    } else if(tools_app.closeFlashlightFromInput()) {
        return;
    } else if(power_menu_visible) {
        close_power_menu();
        return;
    } else if(alarm_ringing) {
        dismiss_alarm_alert();
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
        if(factory_reset_worker_active.load(std::memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const unsigned long now = millis();

        const firefly::ButtonAction boot_action = poll_boot_button(now);
        if(boot_action == firefly::ButtonAction::ShortPress) {
            post_background_system_event({firefly::EventType::ShortPress,
                                          0,
                                          now,
                                          firefly::EventPriority::Critical});
        }

        const firefly::PowerButtonEvent power_action = poll_power_button(now);
        if(power_action != firefly::PowerButtonEvent::None) {
            const uint32_t value = power_action == firefly::PowerButtonEvent::LongPress
                ? static_cast<uint32_t>(firefly::ButtonAction::LongPress)
                : static_cast<uint32_t>(firefly::ButtonAction::ShortPress);
            post_background_system_event({firefly::EventType::PowerPress,
                                          value,
                                          now,
                                          firefly::EventPriority::Critical});
        }

        if(poll_motion_source(now)) {
            post_background_system_event({firefly::EventType::Wake,
                                          0,
                                          now,
                                          firefly::EventPriority::Critical});
        }

        const firefly::PowerMode power_mode = evaluate_runtime_power_mode(now);
        if(power_mode == firefly::PowerMode::ScreenOff &&
           is_sleeping && !sleep_display_off) {
            post_background_system_event({firefly::EventType::SleepBlackout,
                                          0,
                                          now,
                                          firefly::EventPriority::Refresh});
        } else if(power_mode == firefly::PowerMode::Glance && !is_sleeping) {
            post_background_system_event({firefly::EventType::EnterSleep,
                                          0,
                                          now,
                                          firefly::EventPriority::Refresh});
        }

        connectivity_service.service(now);
        wifi_service.tick(now);
        service_network_time(now);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void firefly_weather_task(void * parameter) {
    LV_UNUSED(parameter);
    for(;;) {
        if(factory_reset_worker_active.load(std::memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }
        weather_service.tick(millis(), static_cast<int64_t>(time(nullptr)));
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

void firefly_bulk_task(void * parameter) {
    LV_UNUSED(parameter);
    for(;;) {
        if(factory_reset_worker_active.load(std::memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        bulk_transfer_service.tick(millis());
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void firefly_update_task(void * parameter) {
    LV_UNUSED(parameter);
    for(;;) {
        if(factory_reset_worker_active.load(std::memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }
        update_coordinator.runOnce(millis(), firefly_current_update_gate());
        vTaskDelay(pdMS_TO_TICKS(5));
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

void copy_service_alarm_to_legacy(uint8_t slot,
                                  const firefly::Alarm & alarm) {
    if(slot >= FIREFLY_ALARM_SLOT_COUNT) return;
    FireflyAlarm & legacy = firefly_alarms[slot];
    legacy.configured = alarm.configured;
    legacy.enabled = alarm.enabled;
    legacy.hour = alarm.hour;
    legacy.minute = alarm.minute;
    legacy.days_mask = alarm.days_mask;
    legacy.ringtone_index = alarm.ringtone;
    strlcpy(legacy.name, alarm.name, sizeof(legacy.name));
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
    String detail = "It is " + current_time;
    detail += "\n";
    detail += firefly_alarm_day_label(alarm.days_mask);
    detail += "  ";
    detail += firefly_alarm_ringtone_name(alarm.ringtone_index);
    detail += "\nVolume " + String(volume_level) + "%";
    if(alarm_overlay_detail) lv_label_set_text(alarm_overlay_detail, detail.c_str());
    ui_shell.showOverlay(firefly::SystemOverlayHost::kAlarmPriority,
                         alarm_overlay);

    const firefly::AlarmToneResource & tone =
        firefly::AlarmService::ringtoneResource(alarm.ringtone_index);
    const bool sound_started =
        system_capabilities.has(firefly::Capability::Audio) &&
        audio_service.startLoopingPcm(tone.samples, tone.frames,
                                      tone.sample_rate,
                                      firefly::AudioUse::Alarm);
    if(!sound_started && alarm_overlay_detail) {
        constexpr const char * unavailable = "Sound unavailable";
        detail += "\n";
        detail += unavailable;
        lv_label_set_text(alarm_overlay_detail, detail.c_str());
    }
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

int64_t local_setting_changed_at_ms() {
    const time_t epoch = time(nullptr);
    return epoch >= 1577836800
        ? static_cast<int64_t>(epoch) * 1000LL
        : static_cast<int64_t>(millis());
}

bool record_local_brightness(uint8_t brightness) {
    return companion_sync_service.recordLocalSetting(
        firefly::CompanionSettingKind::Brightness,
        &brightness,
        sizeof(brightness),
        local_setting_changed_at_ms()
    );
}

bool record_local_volume(uint8_t volume) {
    return companion_sync_service.recordLocalSetting(
        firefly::CompanionSettingKind::Volume,
        &volume,
        sizeof(volume),
        local_setting_changed_at_ms()
    );
}

bool record_local_alarm(uint8_t slot, const firefly::Alarm & alarm) {
    const size_t name_length = strnlen(alarm.name, sizeof(alarm.name));
    if(slot >= firefly::AlarmService::kSlots ||
       name_length >= sizeof(alarm.name)) {
        return false;
    }
    uint8_t value[8 + sizeof(alarm.name)]{};
    value[0] = slot;
    value[1] = alarm.configured ? 1 : 0;
    value[2] = alarm.enabled ? 1 : 0;
    value[3] = alarm.hour;
    value[4] = alarm.minute;
    value[5] = alarm.days_mask;
    value[6] = alarm.ringtone;
    value[7] = static_cast<uint8_t>(name_length);
    if(name_length > 0) memcpy(value + 8, alarm.name, name_length);
    return companion_sync_service.recordLocalSetting(
        firefly::CompanionSettingKind::Alarm,
        value,
        static_cast<uint16_t>(8 + name_length),
        local_setting_changed_at_ms()
    );
}

bool record_local_theme(const char * theme_id) {
    if(!theme_id) return false;
    const size_t length = strnlen(theme_id, sizeof(system_settings.theme_id));
    if(length == 0 || length >= sizeof(system_settings.theme_id)) return false;
    return companion_sync_service.recordLocalSetting(
        firefly::CompanionSettingKind::Theme,
        reinterpret_cast<const uint8_t *>(theme_id),
        static_cast<uint16_t>(length),
        local_setting_changed_at_ms()
    );
}

} // namespace

firefly::UpdateRuntimeGate firefly_current_update_gate() {
    const firefly::SystemState state = ui_state_store.snapshot();
    const firefly::AudioUse audio = audio_service.activeUse();
    const firefly::BulkTransferState transfer =
        bulk_transfer_service.snapshot().state;
    firefly::UpdateRuntimeGate gate{};
    gate.battery_valid = state.battery.valid;
    gate.battery_percent = state.battery.percent;
    gate.charging = state.battery.charging || state.battery.vbus_present;
    gate.alarm_active = alarm_ringing.load(std::memory_order_acquire);
    gate.music_active = audio == firefly::AudioUse::Music;
    gate.recording_active = audio == firefly::AudioUse::Recorder;
    gate.transfer_active =
        transfer == firefly::BulkTransferState::WaitingForNetwork ||
        transfer == firefly::BulkTransferState::Ready ||
        transfer == firefly::BulkTransferState::Receiving;
    return gate;
}

bool firefly_audio_start_allowed(firefly::AudioUse use, void *) {
    if(system_resources.held(firefly::ResourceKind::Ota)) {
        if(use == firefly::AudioUse::Alarm && update_service.cancel(millis())) {
            return true;
        }
        return false;
    }
    const firefly::BulkTransferState state =
        bulk_transfer_service.snapshot().state;
    const bool transfer_active =
        state == firefly::BulkTransferState::WaitingForNetwork ||
        state == firefly::BulkTransferState::Ready ||
        state == firefly::BulkTransferState::Receiving ||
        state == firefly::BulkTransferState::Completed;
    if(!transfer_active) return true;
    if(use == firefly::AudioUse::Alarm) {
        bulk_transfer_service.cancel(
            firefly::BulkTransferFailure::AudioBusy, millis());
        return true;
    }
    return false;
}

bool firefly_apply_local_music_volume(uint8_t volume) {
    if(volume > 100 || !record_local_volume(volume)) {
        ++settings_command_failures;
        return false;
    }
    if(!storage_service.saveSettings(system_settings)) {
        ++settings_command_failures;
    }
    return true;
}

bool FireflyCompanionSettingsPersistence::saveSnapshot(
    const firefly::CompanionSettingsSnapshot & snapshot) {
    uint8_t alarm_slot = 0;
    firefly::Alarm decoded_alarm{};
    bool has_alarm = false;
    firefly::SystemSettings next = system_settings;
    if(!prepare_companion_settings_snapshot(
           snapshot, next, has_alarm, alarm_slot, decoded_alarm)) {
        return false;
    }

    constexpr uint8_t theme_index =
        static_cast<uint8_t>(firefly::CompanionSettingKind::Theme) - 1;
    const bool has_theme = snapshot.valid[theme_index] == 1;
    uint32_t theme_palette[5]{};
    if(has_theme &&
       !resolve_remote_theme_palette(next.theme_id, theme_palette)) {
        return false;
    }

    if(!storage_service.saveCompanionSettingsSnapshot(
           &snapshot, sizeof(snapshot))) {
        return false;
    }
    system_settings = next;
    screen_brightness = next.brightness;
    volume_level = next.volume;
    if(has_alarm) {
        alarm_service.set(alarm_slot, decoded_alarm);
        copy_service_alarm_to_legacy(alarm_slot, decoded_alarm);
        clear_alarm_trigger_history();
    }
    set_screen_brightness_level(screen_brightness);
    audio_service.setVolume(volume_level);
    if(has_theme) apply_runtime_theme_palette(theme_palette);
    refresh_sound_alarm_ui();
    return true;
}

bool FireflyCompanionSettingsPersistence::restoreSnapshot(
    const firefly::CompanionSettingsSnapshot & snapshot) {
    uint8_t alarm_slot = 0;
    firefly::Alarm decoded_alarm{};
    bool has_alarm = false;
    firefly::SystemSettings next = system_settings;
    if(!prepare_companion_settings_snapshot(
           snapshot, next, has_alarm, alarm_slot, decoded_alarm)) {
        return false;
    }
    constexpr uint8_t theme_index =
        static_cast<uint8_t>(firefly::CompanionSettingKind::Theme) - 1;
    const bool has_theme = snapshot.valid[theme_index] == 1;
    uint32_t theme_palette[5]{};
    if(has_theme &&
       !resolve_remote_theme_palette(next.theme_id, theme_palette)) {
        return false;
    }
    system_settings = next;
    screen_brightness = next.brightness;
    volume_level = next.volume;
    if(has_alarm) {
        if(!alarm_service.set(alarm_slot, decoded_alarm)) return false;
        copy_service_alarm_to_legacy(alarm_slot, decoded_alarm);
    }
    if(has_theme) apply_runtime_theme_palette(theme_palette);
    return true;
}

bool firefly_send_phone_media_command(
    firefly::RemoteMediaCommand command,
    uint8_t volume_percent) {
    if(!connectivity_service.connected() || !connectivity_service.paired()) {
        return false;
    }
    firefly::protocol::Frame frame{};
    if(!firefly::CompanionSyncService::buildMediaCommand(
           command,
           volume_percent,
           connectivity_service.allocateOutgoingSequence(),
           frame)) {
        return false;
    }
    return connectivity_service.send(frame, millis());
}

bool firefly_send_find_phone_command() {
    if(!connectivity_service.connected() || !connectivity_service.paired()) {
        return false;
    }
    firefly::protocol::Frame frame{};
    if(!firefly::CompanionSyncService::buildFindPhone(
           connectivity_service.allocateOutgoingSequence(), frame)) {
        return false;
    }
    return connectivity_service.send(frame, millis());
}

void configure_power_sleep_hooks() {
    power_service.setSleepHooks({prepare_verified_light_sleep,
                                 restore_verified_light_sleep});
}

void load_motion_summary_preference() {
    if(motion_summary_preference_loaded) return;
    const uint32_t today = current_local_day_key();
    if(today == 0) return;
    motion_summary_preference_loaded = true;
    firefly::ActivityStats stats{};
    storage_service.loadActivityStats(stats);
    if(stats.day_key != today) {
        motion_service.setDayKey(today);
        motion_last_saved_day = today;
        motion_last_saved_steps = 0;
        motion_last_saved_active_minutes = 0;
        motion_last_saved_at = millis();
        return;
    }
    motion_service.restoreDailySummary(today, stats.steps, stats.active_minutes);
    motion_last_saved_day = today;
    motion_last_saved_steps = stats.steps;
    motion_last_saved_active_minutes = stats.active_minutes;
    motion_last_saved_at = millis();
}

void persist_motion_summary(bool force) {
    const uint32_t now = millis();
    if(!force && motion_last_saved_at != 0 &&
       now - motion_last_saved_at < 15UL * 60UL * 1000UL) {
        return;
    }

    const uint32_t today = current_local_day_key();
    if(today == 0) return;
    const firefly::MotionSummary summary = motion_service.summary();
    if(today != motion_last_saved_day ||
       summary.steps != motion_last_saved_steps ||
       summary.active_minutes != motion_last_saved_active_minutes) {
        firefly::ActivityStats stats{};
        stats.schema_version = firefly::StorageService::kSchemaVersion;
        stats.day_key = today;
        stats.steps = summary.steps;
        stats.active_minutes = summary.active_minutes;
        if(storage_service.saveActivityStats(stats)) {
            motion_last_saved_day = today;
            motion_last_saved_steps = summary.steps;
            motion_last_saved_active_minutes = summary.active_minutes;
        }

        file_scan_service.service(system_event_bus, now);
    }
    motion_last_saved_at = now;
}

void start_firefly_background_task() {
    if(!firefly_update_task_handle) {
#if defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE
        const BaseType_t update_core = 0;
#else
        const BaseType_t update_ui_core = xPortGetCoreID();
        const BaseType_t update_core = update_ui_core == 0 ? 1 : 0;
#endif
        firefly_update_task_handle = xTaskCreateStaticPinnedToCore(
            firefly_update_task, "firefly_update", kUpdateTaskStackWords,
            nullptr, 1, update_task_stack, &update_task_storage, update_core);
    }
    firefly_update_task_running = firefly_update_task_handle != NULL;
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
    if(!firefly_weather_task_handle) {
        firefly_weather_task_running = xTaskCreatePinnedToCore(
            firefly_weather_task, "firefly_weather", 6144, NULL, 1,
            &firefly_weather_task_handle, background_core) == pdPASS;
        if(!firefly_weather_task_running) firefly_weather_task_handle = NULL;
    } else {
        firefly_weather_task_running = true;
    }
    if(!firefly_bulk_task_handle) {
        firefly_bulk_task_running = xTaskCreatePinnedToCore(
            firefly_bulk_task, "firefly_bulk", 6144, NULL, 1,
            &firefly_bulk_task_handle, background_core) == pdPASS;
        if(!firefly_bulk_task_running) firefly_bulk_task_handle = NULL;
    } else {
        firefly_bulk_task_running = true;
    }
#endif
}

void firefly_process_connectivity() {
    if(!firefly_background_task_running) {
        const uint32_t now = millis();
        connectivity_service.service(now);
        wifi_service.tick(now);
        service_network_time(now);
    }
    const uint32_t now = millis();
    if(!firefly_weather_task_running) {
        weather_service.tick(now, static_cast<int64_t>(time(nullptr)));
    }
    if(!firefly_bulk_task_running) bulk_transfer_service.tick(now);
    if(!wifi_service.hardwareAvailable()) {
        system_capabilities.set(firefly::Capability::Wifi, false);
        hardware_capabilities.set(
            firefly::HardwareDevice::Wifi,
            firefly::HardwareAvailability::Unavailable,
            firefly::HardwareFailure::IoFailure);
    }
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
    storage_service.loadSettings(system_settings);
    volume_level = system_settings.volume;
    if(volume_level > 100) volume_level = 100;
    screen_brightness = system_settings.brightness;
    auto_sleep_ms = static_cast<uint32_t>(
        system_settings.auto_sleep_seconds) * 1000UL;

    for(uint8_t slot = 0; slot < FIREFLY_ALARM_SLOT_COUNT; ++slot) {
        FireflyAlarm & alarm = firefly_alarms[slot];
        firefly_alarm_reset(alarm, slot);
        firefly::Alarm stored{};
        bool present = false;
        if(storage_service.loadAlarm(slot, stored, present) && present) {
            copy_service_alarm_to_legacy(slot, stored);
        }
    }

    sync_alarm_service_from_legacy();
    clear_alarm_trigger_history();
}

void load_companion_settings_preferences() {
    firefly::CompanionSettingsSnapshot snapshot{};
    size_t length = 0;
    bool present = false;
    if(storage_service.loadCompanionSettingsSnapshot(
           &snapshot, sizeof(snapshot), length, present) &&
       present && length == sizeof(snapshot) &&
       companion_sync_service.restoreSnapshot(snapshot)) {
        const firefly::CompanionSettingsSnapshot normalized =
            companion_sync_service.settingsSnapshot();
        companion_settings_persistence.restoreSnapshot(normalized);
        if(memcmp(&snapshot, &normalized, sizeof(snapshot)) != 0) {
            storage_service.saveCompanionSettingsSnapshot(
                &normalized, sizeof(normalized));
        }
    }
}

void save_volume_preference() {
    system_settings.volume = volume_level;
    storage_service.saveSettings(system_settings);
}

void save_alarm_preferences() {
    for(uint8_t slot = 0; slot < FIREFLY_ALARM_SLOT_COUNT; ++slot) {
        storage_service.saveAlarm(
            slot, service_alarm_from_legacy(firefly_alarms[slot]));
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
    if(settings_reset_container) lv_obj_add_flag(settings_reset_container, LV_OBJ_FLAG_HIDDEN);

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
    activity_app.refresh(motion_service.summary());
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
    activity_app.refresh(motion_service.summary());
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
    if(audio_service.activeUse() == firefly::AudioUse::Alarm) {
        audio_service.stop();
    }
    ui_shell.closeOverlay(alarm_overlay);
}

void show_timer_alert() {
    alarm_ringing = true;
    if(is_sleeping) exit_sleep_screen_mode();
    if(alarm_overlay_title) lv_label_set_text(alarm_overlay_title, "Timer");
    if(alarm_overlay_detail) {
        lv_label_set_text(alarm_overlay_detail,
                          "Countdown complete\nTap dismiss to continue");
    }
    ui_shell.showOverlay(firefly::SystemOverlayHost::kAlarmPriority,
                         alarm_overlay);
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
    load_motion_summary_preference();
    persist_motion_summary(false);
    if(activity_app_active) {
        activity_app.refresh(motion_service.summary());
    }
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

    if(!alarm_ringing) {
        alarm_service.publishTrigger(current_epoch, millis(), system_event_bus);
    }

    refresh_sound_alarm_ui();
    refresh_battery_ui();
}

void enter_sleep_screen_mode() {
    if(is_sleeping || !sleep_screen) {
        return;
    }

    persist_motion_summary(true);
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
    service_find_watch_feedback(millis());
    if(!firefly_background_task_running) {
        const unsigned long now = millis();
        const firefly::ButtonAction boot_action = poll_boot_button(now);
        if(boot_action == firefly::ButtonAction::ShortPress) {
            run_short_press_action();
        }
        const firefly::PowerButtonEvent power_action = poll_power_button(now);
        if(power_action == firefly::PowerButtonEvent::ShortPress) {
            run_power_press_action(firefly::ButtonAction::ShortPress);
        } else if(power_action == firefly::PowerButtonEvent::LongPress) {
            run_power_press_action(firefly::ButtonAction::LongPress);
        }
        if(poll_motion_source(now) && is_sleeping && sleep_display_off) {
            wake_sleep_screen_from_blackout();
        }
        const firefly::PowerMode power_mode = evaluate_runtime_power_mode(now);
        if(power_mode == firefly::PowerMode::ScreenOff &&
           is_sleeping && !sleep_display_off) {
            apply_sleep_blackout();
        } else if(power_mode == firefly::PowerMode::Glance && !is_sleeping) {
            enter_sleep_screen_mode();
        }
    }

    firefly::SystemEvent event{};
    while(system_event_bus.take(event)) {
        switch(event.type) {
            case firefly::EventType::ShortPress:
                run_short_press_action();
                break;
            case firefly::EventType::PowerPress:
                run_power_press_action(
                    static_cast<firefly::ButtonAction>(event.value));
                break;
            case firefly::EventType::Wake:
                if(is_sleeping && sleep_display_off) {
                    wake_sleep_screen_from_blackout();
                }
                break;
            case firefly::EventType::EnterSleep:
                if(!is_sleeping) {
                    enter_sleep_screen_mode();
                }
                break;
            case firefly::EventType::SleepBlackout:
                apply_sleep_blackout();
                break;
            case firefly::EventType::TimerExpired:
                show_timer_alert();
                break;
            case firefly::EventType::AlarmTriggered: {
                const uint8_t slot = static_cast<uint8_t>(event.value);
                if(slot < FIREFLY_ALARM_SLOT_COUNT && !alarm_ringing) {
                    const time_t now_epoch = time(nullptr);
                    struct tm local{};
                    char alarm_time[8] = "--:--";
                    if(localtime_r(&now_epoch, &local)) {
                        snprintf(alarm_time, sizeof(alarm_time), "%02d:%02d",
                                 local.tm_hour, local.tm_min);
                    }
                    firefly_alarm_last_trigger_keys[slot] =
                        String(slot) + " " + alarm_time;
                    trigger_alarm_alert(slot, alarm_time);
                }
                break;
            }
            case firefly::EventType::FilesPageReady:
                files_app.onPageReady();
                break;
            case firefly::EventType::BleMessageReceived: {
                firefly::protocol::Frame frame{};
                if(!connectivity_service.takeReceivedFrame(frame)) break;
                if(frame.type == firefly::protocol::MessageType::Error) {
                    firefly::CompanionRemoteError error{};
                    const char * status = "PHONE ERROR";
                    if(firefly::CompanionSyncService::decodeError(
                           frame, error)) {
                        status =
                            firefly::CompanionSyncService::remoteErrorText(
                                error);
                    }
                    strlcpy(companion_error_status, status,
                            sizeof(companion_error_status));
                    companion_error_status_until = millis() + 5000UL;
                    break;
                }
                if(frame.type == firefly::protocol::MessageType::WifiProvision) {
                    const firefly::TimeSnapshot local_time = time_service.now();
                    const int64_t now_epoch = local_time.valid
                        ? local_time.epoch_seconds : 0;
                    if(wifi_service.stageProvisioning(
                           frame.payload, frame.payload_length,
                           millis(), now_epoch)) {
                        const firefly::WifiProvisioningSnapshot request =
                            wifi_service.provisioningSnapshot();
                        wifi_provision_overlay.showRequest(
                            request.ssid,
                            request.status ==
                                firefly::WifiProvisioningStatus::AwaitingForget);
                        ui_shell.showOverlay(
                            firefly::SystemOverlayHost::kPairingPriority,
                            wifi_provision_overlay.root());
                    } else {
                        firefly::protocol::Frame error{};
                        error.type = firefly::protocol::MessageType::Error;
                        error.sequence = frame.sequence;
                        error.payload[0] = 1;
                        error.payload[1] = static_cast<uint8_t>(frame.type);
                        error.payload[2] = static_cast<uint8_t>(
                            firefly::protocol::WireErrorCode::InvalidPayload);
                        error.payload_length = 3;
                        connectivity_service.send(error, millis());
                    }
                    break;
                }
                if(frame.type == firefly::protocol::MessageType::BulkTransfer) {
                    bool valid_request = false;
                    bool cancel_request = false;
                    firefly::BulkTransferRequest request{};
                    if(frame.payload_length == 4 && frame.payload[0] == 2 &&
                       frame.payload[1] == 4) {
                        request.request_id = static_cast<uint16_t>(
                            frame.payload[2] |
                            (static_cast<uint16_t>(frame.payload[3]) << 8));
                        cancel_request = request.request_id != 0;
                        valid_request = cancel_request;
                    } else if(frame.payload_length >= 47 &&
                              frame.payload[0] == 2 &&
                              frame.payload[1] == 1 &&
                              frame.payload[4] <= 1) {
                        request.request_id = static_cast<uint16_t>(
                            frame.payload[2] |
                            (static_cast<uint16_t>(frame.payload[3]) << 8));
                        request.prefer_shared_lan = frame.payload[4] == 1;
                        uint64_t declared_size = 0;
                        for(uint8_t index = 0; index < 8; ++index) {
                            declared_size |= static_cast<uint64_t>(
                                frame.payload[5 + index]) << (index * 8);
                        }
                        request.declared_size = declared_size;
                        memcpy(request.expected_sha256, frame.payload + 13,
                               sizeof(request.expected_sha256));
                        const uint8_t path_length = frame.payload[45];
                        valid_request = request.request_id != 0 &&
                            path_length > 0 &&
                            path_length < sizeof(request.managed_path) &&
                            frame.payload_length ==
                                static_cast<uint16_t>(46 + path_length);
                        for(uint16_t index = 0;
                            valid_request && index < path_length; ++index) {
                            const uint8_t value = frame.payload[46 + index];
                            if(value < 0x20 || value == 0x7F) {
                                valid_request = false;
                            }
                        }
                        if(valid_request) {
                            memcpy(request.managed_path, frame.payload + 46,
                                   path_length);
                            request.managed_path[path_length] = '\0';
                        }
                    }
                    request.audio_active =
                        audio_service.activeUse() != firefly::AudioUse::None;
                    request.ota_active =
                        system_resources.held(firefly::ResourceKind::Ota);
                    if(!valid_request) {
                        firefly::protocol::Frame error{};
                        error.type = firefly::protocol::MessageType::Error;
                        error.sequence = frame.sequence;
                        error.payload[0] = 1;
                        error.payload[1] = static_cast<uint8_t>(frame.type);
                        error.payload[2] = static_cast<uint8_t>(
                            firefly::protocol::WireErrorCode::InvalidPayload);
                        error.payload_length = 3;
                        connectivity_service.send(error, millis());
                    } else if(cancel_request) {
                        bulk_transfer_service.cancelSession(request.request_id,
                                                            millis());
                    } else {
                        bulk_transfer_service.startSession(request, millis());
                    }
                    break;
                }
                if(frame.type == firefly::protocol::MessageType::WeatherUpdate &&
                   !weather_service.applyPhonePayload(
                       frame.payload, frame.payload_length,
                       static_cast<int64_t>(time(nullptr)))) {
                    firefly::protocol::Frame error{};
                    error.type = firefly::protocol::MessageType::Error;
                    error.sequence = frame.sequence;
                    error.payload[0] = 1;
                    error.payload[1] = static_cast<uint8_t>(frame.type);
                    error.payload[2] = static_cast<uint8_t>(
                        firefly::protocol::WireErrorCode::InvalidPayload);
                    error.payload_length = 3;
                    connectivity_service.send(error, millis());
                    break;
                }
                const firefly::SystemState state = ui_state_store.snapshot();
                const firefly::CompanionDispatchResult result =
                    companion_frame_dispatcher.dispatch(
                        frame, millis(), state.battery.percent
                    );
                if(result ==
                   firefly::CompanionDispatchResult::Notification) {
                    refresh_notification_center_from_service();
                } else if(result ==
                          firefly::CompanionDispatchResult::Companion &&
                          frame.type ==
                              firefly::protocol::MessageType::SettingsGet) {
                    firefly::protocol::Frame response{};
                    if(firefly::CompanionSyncService::buildSettingsSnapshot(
                           companion_sync_service.settingsSnapshot(),
                           connectivity_service.allocateOutgoingSequence(),
                           response)) {
                        connectivity_service.send(response, millis());
                    }
                } else if(result ==
                          firefly::CompanionDispatchResult::Companion &&
                          frame.type ==
                              firefly::protocol::MessageType::CalendarUpdate) {
                    update_companion_calendar_ui();
                } else if(result ==
                          firefly::CompanionDispatchResult::Invalid) {
                    firefly::protocol::Frame error{};
                    error.type = firefly::protocol::MessageType::Error;
                    error.sequence = frame.sequence;
                    error.payload[0] = 1;
                    error.payload[1] = static_cast<uint8_t>(frame.type);
                    error.payload[2] = static_cast<uint8_t>(
                        firefly::protocol::WireErrorCode::InvalidPayload);
                    error.payload_length = 3;
                    connectivity_service.send(error, millis());
                }
                break;
            }
            case firefly::EventType::PhoneConnectionChanged:
                notification_service.setPhoneConnected(event.value != 0);
                if(event.value == 0) {
                    const firefly::BulkTransferState bulk_state =
                        bulk_transfer_service.snapshot().state;
                    if(bulk_state == firefly::BulkTransferState::Ready ||
                       bulk_state == firefly::BulkTransferState::Receiving ||
                       bulk_state ==
                           firefly::BulkTransferState::WaitingForNetwork) {
                        bulk_transfer_service.cancel(
                            firefly::BulkTransferFailure::Disconnected,
                            millis());
                    }
                }
                break;
            case firefly::EventType::PairingRequested: {
                const firefly::PairingSnapshot snapshot =
                    connectivity_service.pairingSnapshot();
                pairing_overlay.showRequest(snapshot.phone_name,
                                            snapshot.passkey);
                ui_shell.showOverlay(
                    firefly::SystemOverlayHost::kPairingPriority,
                    pairing_overlay.root()
                );
                break;
            }
            case firefly::EventType::PairingResult: {
                const firefly::PairingSnapshot snapshot =
                    connectivity_service.pairingSnapshot();
                pairing_overlay.showResult(event.value == 1,
                                           snapshot.phone_name);
                ui_shell.showOverlay(
                    firefly::SystemOverlayHost::kPairingPriority,
                    pairing_overlay.root()
                );
                break;
            }
            case firefly::EventType::UnpairConfirmationRequested: {
                const firefly::PairingSnapshot snapshot =
                    connectivity_service.pairingSnapshot();
                pairing_overlay.showUnpairConfirmation(snapshot.phone_name);
                ui_shell.showOverlay(
                    firefly::SystemOverlayHost::kPairingPriority,
                    pairing_overlay.root()
                );
                break;
            }
            case firefly::EventType::PairingUnbound:
                if(event.value == 1) {
                    notification_service.clearLocal();
                    refresh_notification_center_from_service();
                }
                ui_shell.closeOverlay(pairing_overlay.root());
                break;
            case firefly::EventType::SdRemoved: {
                requestSdRemovalCleanup();
                break;
            }
            default:
                break;
        }
    }

    static firefly::WifiProvisioningStatus last_wifi_status =
        firefly::WifiProvisioningStatus::Idle;
    const firefly::WifiProvisioningSnapshot wifi_snapshot =
        wifi_service.provisioningSnapshot();
    if(wifi_snapshot.status != last_wifi_status) {
        last_wifi_status = wifi_snapshot.status;
        uint8_t wire_status = 0;
        const char * title = nullptr;
        const char * detail = nullptr;
        bool success = false;
        switch(wifi_snapshot.status) {
            case firefly::WifiProvisioningStatus::Connecting:
                wire_status = 1;
                break;
            case firefly::WifiProvisioningStatus::Success:
                wire_status = 2;
                title = "Wi-Fi connected";
                detail = "The credential was saved securely.";
                success = true;
                wifi_service.request(firefly::WifiPurpose::Ntp, millis());
                break;
            case firefly::WifiProvisioningStatus::AuthFailed:
                wire_status = 3;
                title = "Authentication failed";
                detail = "Check the password on the phone and send it again.";
                break;
            case firefly::WifiProvisioningStatus::NotFound:
                wire_status = 4;
                title = "Network not found";
                detail = "Check the network name and signal coverage.";
                break;
            case firefly::WifiProvisioningStatus::Timeout:
                wire_status = 5;
                title = "Connection timed out";
                detail = "FireflyOS will not retry indefinitely.";
                break;
            case firefly::WifiProvisioningStatus::Forgotten:
                wire_status = 6;
                title = "Network forgotten";
                detail = "The saved credential was cleared.";
                success = true;
                break;
            case firefly::WifiProvisioningStatus::Denied:
                wire_status = 7;
                title = "Request cancelled";
                detail = "No Wi-Fi credential was changed.";
                break;
            case firefly::WifiProvisioningStatus::PersistenceFailed:
                wire_status = 8;
                title = "Credential update failed";
                detail = "Saved network data may be unchanged. Try again.";
                break;
            case firefly::WifiProvisioningStatus::Busy:
                wire_status = 9;
                title = "Wi-Fi is busy";
                detail = "Finish the active network task, then send the request again.";
                break;
            default:
                break;
        }
        if(wire_status != 0 && connectivity_service.connected()) {
            firefly::protocol::Frame response{};
            response.type = firefly::protocol::MessageType::WifiProvision;
            response.sequence =
                connectivity_service.allocateOutgoingSequence();
            response.payload[0] = 1;
            response.payload[1] = wire_status;
            response.payload_length = 2;
            connectivity_service.send(response, millis());
        }
        if(title) {
            wifi_provision_overlay.showResult(title, detail, success);
            ui_shell.showOverlay(
                firefly::SystemOverlayHost::kPairingPriority,
                wifi_provision_overlay.root());
        }
    }

    static uint32_t last_bulk_result_generation = 0;
    static uint32_t last_bulk_overlay_generation = 0;
    const firefly::BulkTransferSnapshot bulk = bulk_transfer_service.snapshot();
    if(bulk.result_generation != last_bulk_result_generation &&
       connectivity_service.connected()) {
        firefly::protocol::Frame response{};
        response.type = firefly::protocol::MessageType::BulkTransfer;
        response.sequence = connectivity_service.allocateOutgoingSequence();
        if(bulk.result_state == firefly::BulkTransferState::Ready &&
           bulk.state == firefly::BulkTransferState::Ready &&
           bulk.result_request_id == bulk.request_id) {
            uint16_t offset = 0;
            response.payload[offset++] = 2;
            response.payload[offset++] = 2;
            response.payload[offset++] = static_cast<uint8_t>(
                bulk.result_request_id & 0xFF);
            response.payload[offset++] = static_cast<uint8_t>(
                bulk.result_request_id >> 8);
            const bool soft_ap = wifi_service.mode() == firefly::WifiMode::SoftAp;
            response.payload[offset++] = soft_ap ? 2 : 1;
            const int32_t remaining = static_cast<int32_t>(
                bulk.expires_at_ms - millis());
            const uint64_t expires = remaining > 0
                ? static_cast<uint32_t>(remaining) : 0;
            for(uint8_t index = 0; index < 8; ++index) {
                response.payload[offset++] = static_cast<uint8_t>(
                    (expires >> (index * 8)) & 0xFF);
            }
            const uint8_t endpoint_length = static_cast<uint8_t>(
                strnlen(bulk.endpoint, sizeof(bulk.endpoint)));
            response.payload[offset++] = endpoint_length;
            memcpy(response.payload + offset, bulk.endpoint, endpoint_length);
            offset += endpoint_length;
            memcpy(response.payload + offset, bulk.token_hex, 32);
            offset += 32;
            if(soft_ap) {
                char ssid[24]{};
                char password[13]{};
                snprintf(ssid, sizeof(ssid), "Firefly-%c%c%c%c",
                         bulk.token_hex[0], bulk.token_hex[1],
                         bulk.token_hex[2], bulk.token_hex[3]);
                memcpy(password, bulk.token_hex + 16, 12);
                const uint8_t ssid_length = strlen(ssid);
                response.payload[offset++] = ssid_length;
                memcpy(response.payload + offset, ssid, ssid_length);
                offset += ssid_length;
                response.payload[offset++] = 12;
                memcpy(response.payload + offset, password, 12);
                offset += 12;
                memset(password, 0, sizeof(password));
            } else {
                response.payload[offset++] = 0;
                response.payload[offset++] = 0;
            }
            response.payload_length = offset;
        } else {
            response.payload[0] = 2;
            response.payload[1] = 3;
            response.payload[2] = static_cast<uint8_t>(
                bulk.result_request_id & 0xFF);
            response.payload[3] = static_cast<uint8_t>(
                bulk.result_request_id >> 8);
            response.payload[4] = static_cast<uint8_t>(bulk.result_state);
            response.payload[5] = static_cast<uint8_t>(bulk.result_failure);
            response.payload_length = 6;
        }
        if(connectivity_service.send(response, millis())) {
            last_bulk_result_generation = bulk.result_generation;
        }
    }
    const bool terminal_active_result =
        bulk.result_generation != last_bulk_overlay_generation &&
        bulk.result_request_id == bulk.request_id &&
        bulk.result_state == bulk.state &&
        (bulk.state == firefly::BulkTransferState::Completed ||
         bulk.state == firefly::BulkTransferState::Cancelled ||
         bulk.state == firefly::BulkTransferState::Error);
    if(terminal_active_result) {
            last_bulk_overlay_generation = bulk.result_generation;
            const bool complete = bulk.state ==
                firefly::BulkTransferState::Completed;
            const char * transfer_detail =
                "The transfer ended safely; no part file remains.";
            switch(bulk.result_failure) {
                case firefly::BulkTransferFailure::LowPower:
                    transfer_detail = "Battery level no longer permits transfer.";
                    break;
                case firefly::BulkTransferFailure::SdUnavailable:
                    transfer_detail = "The SD card is unavailable.";
                    break;
                case firefly::BulkTransferFailure::HashMismatch:
                    transfer_detail = "SHA-256 verification failed.";
                    break;
                case firefly::BulkTransferFailure::SizeMismatch:
                    transfer_detail = "The received size did not match.";
                    break;
                case firefly::BulkTransferFailure::Timeout:
                    transfer_detail = "The five-minute idle limit expired.";
                    break;
                case firefly::BulkTransferFailure::Disconnected:
                    transfer_detail = "The authenticated phone disconnected.";
                    break;
                case firefly::BulkTransferFailure::AudioBusy:
                    transfer_detail = "Audio or an alarm needs the SD resource.";
                    break;
                case firefly::BulkTransferFailure::NetworkUnavailable:
                    transfer_detail = "No shared LAN or temporary SoftAP was available.";
                    break;
                default:
                    break;
            }
            wifi_provision_overlay.showResult(
                complete ? "Transfer complete" : "Transfer stopped",
                complete ? "File size and SHA-256 were verified."
                         : transfer_detail,
                complete);
            ui_shell.showOverlay(
                firefly::SystemOverlayHost::kPairingPriority,
                wifi_provision_overlay.root());
    }
}

void factory_reset_worker(void *) {
    for(;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(25));
        const bool completed = factory_reset_service.execute(
            factory_reset_worker_erase_sd.load(std::memory_order_acquire));
        factory_reset_worker_completed.store(completed,
                                             std::memory_order_release);
        factory_reset_worker_finished.store(true, std::memory_order_release);
    }
}

bool firefly_factory_reset_active() {
    return factory_reset_worker_active.load(std::memory_order_acquire);
}

void firefly_process_factory_reset() {
    static uint32_t reboot_ready_at = 0;
    const int8_t request = factory_reset_execute_request.exchange(
        -1, std::memory_order_acq_rel);
    if(request >= 0 && !firefly_factory_reset_active()) {
        if(settings_reset_title) lv_label_set_text(settings_reset_title, "Resetting...");
        if(settings_reset_detail) {
            lv_label_set_text(settings_reset_detail,
                              "Clearing owned data. Do not power off.");
        }
        if(settings_reset_notice) lv_label_set_text(settings_reset_notice, "Working");
        if(settings_reset_keep_button) lv_obj_add_flag(settings_reset_keep_button, LV_OBJ_FLAG_HIDDEN);
        if(settings_reset_sd_button) lv_obj_add_flag(settings_reset_sd_button, LV_OBJ_FLAG_HIDDEN);
        if(settings_reset_cancel_button) lv_obj_add_flag(settings_reset_cancel_button, LV_OBJ_FLAG_HIDDEN);

        audio_service.stop();
        factory_reset_worker_erase_sd.store(
            request == 1, std::memory_order_release);
        factory_reset_worker_completed.store(false, std::memory_order_release);
        factory_reset_worker_finished.store(false, std::memory_order_release);
        factory_reset_worker_active.store(true, std::memory_order_release);
#if defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE
        const BaseType_t reset_core = 0;
#else
        const BaseType_t reset_core = xPortGetCoreID() == 0 ? 1 : 0;
#endif
        if(!factory_reset_task_handle) {
            factory_reset_task_handle = xTaskCreateStaticPinnedToCore(
                factory_reset_worker,
                "factory_reset",
                kFactoryResetTaskStackWords,
                nullptr,
                1,
                factory_reset_task_stack,
                &factory_reset_task_storage,
                reset_core);
        }
        if(!factory_reset_task_handle) {
            factory_reset_worker_active.store(false, std::memory_order_release);
            if(settings_reset_title) lv_label_set_text(settings_reset_title, "Reset failed");
            if(settings_reset_detail) lv_label_set_text(settings_reset_detail, "Cleanup task could not start. Device was not restarted.");
            if(settings_reset_notice) lv_label_set_text(settings_reset_notice, "Error: task start");
            if(settings_reset_cancel_button) lv_obj_clear_flag(settings_reset_cancel_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            xTaskNotifyGive(factory_reset_task_handle);
        }
    }

    if(factory_reset_worker_finished.exchange(false,
                                               std::memory_order_acq_rel)) {
        const bool completed = factory_reset_worker_completed.load(
            std::memory_order_acquire);
        factory_reset_worker_active.store(false, std::memory_order_release);
        notification_center.clear();
        const firefly::FactoryResetSnapshot state = factory_reset_service.snapshot();
        if(!completed) {
            if(settings_reset_title) lv_label_set_text(settings_reset_title, "Reset failed");
            if(settings_reset_detail) {
                lv_label_set_text(settings_reset_detail,
                                  "Data cleanup was incomplete. Device was not restarted.");
            }
            if(settings_reset_notice) {
                lv_label_set_text_fmt(settings_reset_notice, "Error %u",
                    static_cast<unsigned>(state.failure));
            }
            if(settings_reset_cancel_button) {
                lv_obj_clear_flag(settings_reset_cancel_button, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            if(settings_reset_title) lv_label_set_text(settings_reset_title, "Reset complete");
            if(settings_reset_detail) lv_label_set_text(settings_reset_detail, "Restarting with defaults...");
            reboot_ready_at = millis() + 500UL;
        }
    }

    if(factory_reset_reboot_pending.load(std::memory_order_acquire) &&
       reboot_ready_at != 0 &&
       static_cast<int32_t>(millis() - reboot_ready_at) >= 0) {
        ESP.restart();
    }
}

void firefly_process_sd_card() {
    static uint32_t last_check_at = 0;
    static uint32_t last_mount_attempt_at = 0;
    const uint32_t now = millis();

    finishSdRemovalCleanup();
    if(sd_removal_cleanup_pending) return;

    if(sd_card.takeRemovedEvent()) {
        system_capabilities.set(firefly::Capability::Sd, false);
        hardware_capabilities.set(
            firefly::HardwareDevice::Sd,
            firefly::HardwareAvailability::Unavailable,
            firefly::HardwareFailure::IoFailure);
        requestSdRemovalCleanup();
        post_background_system_event({
            firefly::EventType::SdRemoved,
            0,
            now,
            firefly::EventPriority::Critical
        });
        return;
    }

    if(sd_card.mounted()) {
        if(last_check_at == 0 || now - last_check_at >= 1000UL) {
            last_check_at = now;
            storage_service.validateSdSession();
        }
        return;
    }

    if(last_mount_attempt_at == 0 || now - last_mount_attempt_at >= 5000UL) {
        last_mount_attempt_at = now;
        if(sd_card.begin()) {
            storage_service.attachSd(sd_card.filesystem(), sd_card);
            const uint16_t removed =
                firefly::AudioService::cleanupTemporaryRecordings(storage_service);
            const uint16_t removed_bulk_parts =
                storage_service.cleanupBulkPartFiles();
            system_capabilities.set(firefly::Capability::Sd, true);
            hardware_capabilities.set(
                firefly::HardwareDevice::Sd,
                firefly::HardwareAvailability::Available,
                firefly::HardwareFailure::None);
            post_background_system_event({
                firefly::EventType::CapabilityChanged,
                firefly::capabilityBit(firefly::Capability::Sd),
                now
            });
            Serial.printf("SD card mounted at /FireflyOS; removed %u "
                          "incomplete recording(s) and %u incomplete "
                          "bulk transfer(s).\n", removed, removed_bulk_parts);
        }
    }
}

void firefly_process_clock_sessions() {
    const uint32_t now_ms = millis();
    clock_app.tick(now_ms, esp_timer_get_time());
    if(alarm_ringing || !clock_app.timerExpired(now_ms)) return;
    const firefly::SystemEvent event{
        firefly::EventType::TimerExpired,
        0,
        now_ms,
        firefly::EventPriority::Critical
    };
    if(system_event_bus.post(event)) {
        clock_app.consumeTimerExpired(now_ms);
    }
}

void firefly_process_settings_commands() {
    firefly::SettingsCommand command{};
    while(settings_app.takeCommand(command)) {
        switch(command.type) {
            case firefly::SettingsCommandType::SetBrightness:
            {
                const uint8_t brightness = static_cast<uint8_t>(
                    command.value < 0 ? 0 :
                    (command.value > 255 ? 255 : command.value));
                if(!record_local_brightness(brightness) ||
                   !storage_service.saveSettings(system_settings)) {
                    ++settings_command_failures;
                }
                break;
            }
            case firefly::SettingsCommandType::SetVolume:
            {
                const uint8_t volume = static_cast<uint8_t>(
                    command.value < 0 ? 0 :
                    (command.value > 100 ? 100 : command.value));
                if(!record_local_volume(volume) ||
                   !storage_service.saveSettings(system_settings)) {
                    ++settings_command_failures;
                }
                break;
            }
            case firefly::SettingsCommandType::SetLocalTime:
                if(command.value >= 0 &&
                   time_service.setLocalTime(command.value)) {
                    sync_time_to_system_from_epoch(command.value);
                    clear_alarm_trigger_history();
                    update_time_cb(nullptr);
                }
                break;
            case firefly::SettingsCommandType::ReloadRtc: {
                const firefly::TimeSnapshot snapshot = time_service.reloadRtc();
                if(snapshot.valid) {
                    sync_time_to_system_from_epoch(snapshot.epoch_seconds);
                    clear_alarm_trigger_history();
                    update_time_cb(nullptr);
                }
                break;
            }
            case firefly::SettingsCommandType::SetAutoSleep:
                auto_sleep_ms = static_cast<uint32_t>(
                    command.value < 0 ? 0 : command.value);
                system_settings.auto_sleep_seconds = static_cast<uint16_t>(
                    auto_sleep_ms / 1000UL);
                storage_service.saveSettings(system_settings);
                break;
            case firefly::SettingsCommandType::SaveAlarm:
                if(command.slot >= FIREFLY_ALARM_SLOT_COUNT ||
                   !record_local_alarm(command.slot, command.alarm) ||
                   !storage_service.saveAlarm(command.slot, command.alarm)) {
                    ++settings_command_failures;
                }
                break;
            default:
                break;
        }
    }
}

void firefly_process_power_policy() {
    if(companion_sync_service.findWatchAt(millis()).active) return;
    static uint8_t last_applied_brightness = UINT8_MAX;
    uint8_t effective_brightness = screen_brightness;
    const firefly::PowerMode mode = runtime_power_mode;
    uint8_t cap = UINT8_MAX;
    if(mode == firefly::PowerMode::Saver) cap = 160;
    else if(mode == firefly::PowerMode::LowBattery) cap = 96;
    else if(mode == firefly::PowerMode::CriticalBattery ||
            mode == firefly::PowerMode::ThermalProtection) cap = 64;
    if(effective_brightness > cap) effective_brightness = cap;

    if(!sleep_display_off && effective_brightness != last_applied_brightness) {
        firefly_board.setDisplayBrightness(effective_brightness);
        last_applied_brightness = effective_brightness;
    }
    if(mode == firefly::PowerMode::CriticalBattery ||
       mode == firefly::PowerMode::ThermalProtection) {
        recorder_app.stopForSafety();
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

void firefly_process_media_apps() {
    const bool sd_available =
        system_capabilities.has(firefly::Capability::Sd) && sd_card.mounted();
    static uint32_t last_storage_probe_at = 0;
    static uint64_t total = 0;
    static uint64_t used = 0;
    const uint32_t now = millis();
    if(!sd_available) {
        total = 0;
        used = 0;
    } else if(last_storage_probe_at == 0 ||
              now - last_storage_probe_at >= 1000UL) {
        last_storage_probe_at = now;
        total = storage_service.sdTotalBytes();
        used = storage_service.sdUsedBytes();
    }
    const uint64_t free_bytes = total > used ? total - used : 0;

    files_app.bindStorage(storage_service, file_scan_service, sd_available);
    files_app.tick();
    music_app.bindStorage(storage_service, sd_available);
    music_app.tick(now, runtime_power_mode != firefly::PowerMode::Active);
    recorder_app.bindStorage(storage_service, sd_available, free_bytes);
    recorder_app.tick(now, static_cast<int64_t>(time(nullptr)));
    themes_app.bindStorage(storage_service, sd_available);
    themes_app.tick();

    uint32_t applied_palette[5]{};
    if(themes_app.takeAppliedPalette(applied_palette)) {
        if(!record_local_theme(themes_app.appliedThemeId())) {
            ++settings_command_failures;
            return;
        }
        apply_runtime_theme_palette(applied_palette);
        storage_service.loadSettings(system_settings);
    }

    const char * media_status = "";
    const char * glance_status = "";
    if(recorder_app.recording()) {
        media_status = "REC";
        glance_status = "RECORDING";
    } else if(companion_error_status[0] != '\0' &&
              static_cast<int32_t>(
                  companion_error_status_until - now) > 0) {
        media_status = companion_error_status;
    } else if(music_app.playing()) {
        media_status = "PLAY";
    } else {
        companion_error_status[0] = '\0';
    }
    if(media_status_label) lv_label_set_text(media_status_label, media_status);
    if(sleep_media_status_label) {
        lv_label_set_text(sleep_media_status_label, glance_status);
    }
}

void firefly_refresh_companion_weather_ui() {
    const bool weather_active =
        ui_shell.navigation().current() == firefly::Route::Weather;
    if(!weather_active) return;
    static uint32_t last_refresh_at = 0;
    const uint32_t now_ms = millis();
    if(last_refresh_at != 0 &&
       static_cast<uint32_t>(now_ms - last_refresh_at) < 1000UL) {
        return;
    }
    last_refresh_at = now_ms;
    const bool connected = connectivity_service.connected();
    const firefly::WeatherFreshness freshness =
        weather_service.freshness(static_cast<int64_t>(time(nullptr)));
    weather_app.refresh(
        weather_service.snapshot(static_cast<int64_t>(time(nullptr))),
        freshness,
        weather_service.state(),
        connected);
}

void firefly_report_gate_a_diagnostics() {
    static uint32_t last_report_at = 0;
    const uint32_t now = millis();
    if(now - last_report_at < 10000UL) {
        return;
    }
    last_report_at = now;
    const firefly::MotionDiagnostics motion = motion_service.diagnostics();
    const firefly::StorageDiagnostics storage = storage_service.diagnostics();
    Serial.printf(
        "FIREFLY_GATE_A uptime_ms=%lu internal_free=%u internal_min=%u "
        "psram_free=%u event_post_failures=%lu event_queue=%u "
        "settings_command_failures=%lu desktop_transition_max_ms=%lu "
        "motion_valid=%lu motion_invalid=%lu motion_steps=%lu "
        "motion_wrist_events=%lu storage_failures=%lu\n",
        static_cast<unsigned long>(now),
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        ESP.getFreePsram(),
        static_cast<unsigned long>(event_post_failures),
        static_cast<unsigned>(system_event_bus.size()),
        static_cast<unsigned long>(settings_command_failures),
        static_cast<unsigned long>(desktop_transition_max_ms),
        static_cast<unsigned long>(motion.valid_samples),
        static_cast<unsigned long>(motion.invalid_samples),
        static_cast<unsigned long>(motion.steps),
        static_cast<unsigned long>(motion.wrist_events),
        static_cast<unsigned long>(storage.failures)
    );
}

firefly::DiagnosticSample capture_diagnostic_sample() {
    firefly::DiagnosticSample sample{};
    sample.internal_free = ESP.getFreeHeap();
    sample.internal_minimum = ESP.getMinFreeHeap();
    sample.internal_largest = heap_caps_get_largest_free_block(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    sample.psram_free = ESP.getFreePsram();
    const UBaseType_t ui_words = uxTaskGetStackHighWaterMark(nullptr);
    const UBaseType_t background_words = firefly_background_task_handle
        ? uxTaskGetStackHighWaterMark(firefly_background_task_handle) : 0;
    sample.ui_stack_words = ui_words > UINT16_MAX
        ? UINT16_MAX : static_cast<uint16_t>(ui_words);
    sample.background_stack_words = background_words > UINT16_MAX
        ? UINT16_MAX : static_cast<uint16_t>(background_words);
    sample.event_drops = system_event_bus.droppedCount();
    sample.event_size = system_event_bus.size();
    sample.event_peak = system_event_bus.peakSize();
    sample.power_mode = runtime_power_mode;
    sample.restart_reason = static_cast<uint8_t>(esp_reset_reason());
    return sample;
}

void firefly_sample_diagnostics() {
    const uint32_t now = millis();
    const firefly::DiagnosticSample sample = capture_diagnostic_sample();
    diagnostic_service.sampleMinute(now, sample);

    static bool initialized = false;
    static bool wifi_active = false;
    static bool bulk_active = false;
    static bool ota_active = false;
    static bool audio_active = false;
    static bool recorder_active = false;
    static bool sd_active = false;
    static bool boot_active = false;

    const bool wifi_now = wifi_service.mode() != firefly::WifiMode::Off;
    const firefly::BulkTransferState bulk_state =
        bulk_transfer_service.snapshot().state;
    const bool bulk_now = bulk_state == firefly::BulkTransferState::WaitingForNetwork ||
        bulk_state == firefly::BulkTransferState::Ready ||
        bulk_state == firefly::BulkTransferState::Receiving;
    const firefly::UpdateState update_state = update_service.snapshot().state;
    const bool ota_now = update_state == firefly::UpdateState::Available ||
        update_state == firefly::UpdateState::Downloading ||
        update_state == firefly::UpdateState::Verifying ||
        update_state == firefly::UpdateState::Writing;
    const firefly::AudioUse audio_use = audio_service.activeUse();
    const bool audio_now = audio_use != firefly::AudioUse::None;
    const bool recorder_now = audio_use == firefly::AudioUse::Recorder;
    const bool sd_now = storage_service.sdAvailable();
    const bool boot_now = boot_validation_service.snapshot().state ==
        firefly::BootValidationState::Checking;

    if(!initialized) {
        initialized = true;
        wifi_active = wifi_now;
        bulk_active = bulk_now;
        ota_active = ota_now;
        audio_active = audio_now;
        recorder_active = recorder_now;
        sd_active = sd_now;
        boot_active = boot_now;
        if(sd_now) diagnostic_service.record(
            now, firefly::DiagnosticReason::SdMounted, sample);
        if(boot_now) diagnostic_service.record(
            now, firefly::DiagnosticReason::BootValidationStart, sample);
        return;
    }

    auto note = [&](bool before, bool after,
                    firefly::DiagnosticReason started,
                    firefly::DiagnosticReason ended) {
        if(before != after) diagnostic_service.record(
            now, after ? started : ended, sample);
    };
    note(wifi_active, wifi_now, firefly::DiagnosticReason::WifiStart,
         firefly::DiagnosticReason::WifiEnd);
    note(bulk_active, bulk_now, firefly::DiagnosticReason::BulkStart,
         firefly::DiagnosticReason::BulkEnd);
    note(ota_active, ota_now, firefly::DiagnosticReason::OtaStart,
         firefly::DiagnosticReason::OtaEnd);
    note(audio_active, audio_now, firefly::DiagnosticReason::AudioStart,
         firefly::DiagnosticReason::AudioEnd);
    note(recorder_active, recorder_now, firefly::DiagnosticReason::RecorderStart,
         firefly::DiagnosticReason::RecorderEnd);
    note(sd_active, sd_now, firefly::DiagnosticReason::SdMounted,
         firefly::DiagnosticReason::SdRemoved);
    note(boot_active, boot_now, firefly::DiagnosticReason::BootValidationStart,
         firefly::DiagnosticReason::BootValidationEnd);
    wifi_active = wifi_now;
    bulk_active = bulk_now;
    ota_active = ota_now;
    audio_active = audio_now;
    recorder_active = recorder_now;
    sd_active = sd_now;
    boot_active = boot_now;
}

void firefly_export_diagnostics() {
    const firefly::DiagnosticSample sample = capture_diagnostic_sample();
    diagnostic_service.record(
        millis(), firefly::DiagnosticReason::Manual, sample);
    diagnostic_service.exportTo(serial_diagnostic_export);
    if(!diagnostic_service.exportTo(sd_diagnostic_export)) {
        Serial.println("FIREFLY_DIAGNOSTICS_SD_EXPORT_FAILED");
    }
}
