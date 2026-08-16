#include <Arduino.h>
#include <FireflyOS.h>
#include <mbedtls/sha256.h>
#include <string.h>

static_assert(firefly::ConnectivityService::kDispatchQueueCapacity >= 2,
              "BLE dispatch queue must preserve consecutive business frames");
#include <firefly/apps/clock/ClockApp.h>
#include <firefly/apps/calendar/CalendarApp.h>
#include <firefly/apps/settings/SettingsApp.h>
#include <firefly/apps/tools/ToolsApp.h>
#include <firefly/services/AlarmService.h>
#include <firefly/services/InputService.h>
#include <firefly/services/MotionService.h>
#include <firefly/services/PowerService.h>
#include <firefly/services/WifiService.h>
#include <firefly/services/StorageService.h>
#include <firefly/services/TimeService.h>
#include <firefly/services/ThemePackageService.h>
#include <firefly/services/AudioService.h>
#include <firefly/services/FileScanService.h>
#include <firefly/services/NotificationService.h>
#include <firefly/hal/SdCardDevice.h>
#include <firefly/protocol/FrameCodec.h>
#include <firefly/apps/files/FilesApp.h>
#include <firefly/apps/music/MusicApp.h>
#include <firefly/apps/recorder/RecorderApp.h>
#include <firefly/apps/themes/ThemesApp.h>

static uint16_t failures = 0;

static void expect_true(bool value, const char * name) {
    if(!value) {
        ++failures;
        Serial.printf("FAIL %s\n", name);
    }
}

static firefly::NotificationSummary notification_summary(
    const char * key,
    const char * title,
    int64_t posted_epoch,
    bool pinned = false) {
    firefly::NotificationSummary summary{};
    strlcpy(summary.key, key, sizeof(summary.key));
    strlcpy(summary.app_name, "Messages", sizeof(summary.app_name));
    strlcpy(summary.title, title, sizeof(summary.title));
    strlcpy(summary.body, "body", sizeof(summary.body));
    summary.posted_epoch = posted_epoch;
    summary.pinned = pinned;
    return summary;
}

static void test_notification_service_is_bounded_and_local_only() {
    firefly::NotificationService service;
    firefly::NotificationSummary visible{};

    expect_true(service.push(notification_summary("same", "first", 1)),
                "notification inserts first key");
    expect_true(service.push(notification_summary("same", "updated", 2)),
                "notification updates same key");
    expect_true(service.count() == 1 &&
                    service.copyForDisplay(0, false, visible) &&
                    strcmp(visible.title, "updated") == 0,
                "notification same key updates in place");

    service.clearLocal();
    expect_true(service.count() == 0,
                "notification local clear only clears watch summaries");

    service.push(notification_summary("key-0", "pinned", 0, true));
    for(uint8_t index = 1; index < firefly::NotificationService::kCapacity;
        ++index) {
        char key[16]{};
        snprintf(key, sizeof(key), "key-%u", index);
        service.push(notification_summary(key, key, index));
    }
    expect_true(service.push(notification_summary("key-20", "new", 20)),
                "notification accepts item past capacity");
    expect_true(service.count() == firefly::NotificationService::kCapacity &&
                    service.contains("key-0") && !service.contains("key-1") &&
                    service.contains("key-20"),
                "notification evicts oldest unpinned summary");

    expect_true(service.dismiss("key-20") && !service.contains("key-20"),
                "notification dismiss removes matching key");

    const uint8_t before_disconnect = service.count();
    service.setPhoneConnected(false);
    service.setPhoneConnected(true);
    expect_true(service.count() == before_disconnect,
                "notification disconnect keeps local summaries");

    service.setLockScreenBodyHidden(true);
    expect_true(service.copyForDisplay(0, true, visible) &&
                    visible.body[0] == '\0',
                "notification lock screen body follows local privacy");
}

class FakeCompanionSettingsPersistence
    : public firefly::CompanionSettingsPersistence {
public:
    bool saveSnapshot(
        const firefly::CompanionSettingsSnapshot & snapshot) override {
        ++calls;
        last_snapshot = snapshot;
        return save_result;
    }

    bool save_result = true;
    uint8_t calls = 0;
    firefly::CompanionSettingsSnapshot last_snapshot{};
};

static firefly::VersionedCompanionSetting companion_setting(
    uint32_t revision,
    int64_t changed_at,
    uint8_t value) {
    firefly::VersionedCompanionSetting setting{};
    setting.revision = revision;
    setting.changed_at = changed_at;
    setting.value_length = 1;
    setting.value[0] = value;
    return setting;
}

static firefly::VersionedCompanionSetting companion_alarm_setting(
    uint32_t revision,
    int64_t changed_at,
    uint8_t slot,
    uint8_t hour,
    const char * name) {
    firefly::VersionedCompanionSetting setting{};
    setting.revision = revision;
    setting.changed_at = changed_at;
    const uint8_t name_length = static_cast<uint8_t>(strlen(name));
    setting.value[0] = slot;
    setting.value[1] = 1;
    setting.value[2] = 1;
    setting.value[3] = hour;
    setting.value[4] = 30;
    setting.value[5] = 0x7F;
    setting.value[6] = 0;
    setting.value[7] = name_length;
    memcpy(setting.value + 8, name, name_length);
    setting.value_length = static_cast<uint16_t>(8 + name_length);
    return setting;
}

static void test_companion_settings_resolve_independently_and_persist_first() {
    FakeCompanionSettingsPersistence persistence;
    firefly::CompanionSyncService service(&persistence);

    expect_true(service.applySetting(
                    firefly::CompanionSettingKind::Brightness,
                    companion_setting(7, 700, 70)),
                "companion accepts brightness setting");
    expect_true(service.applySetting(
                    firefly::CompanionSettingKind::Volume,
                    companion_setting(2, 200, 20)),
                "companion accepts volume setting independently");
    expect_true(!service.applySetting(
                    firefly::CompanionSettingKind::Brightness,
                    companion_setting(8, 699, 80)),
                "older explicit operation cannot replace brightness");

    firefly::VersionedCompanionSetting output{};
    expect_true(service.setting(firefly::CompanionSettingKind::Brightness,
                                output) &&
                    output.value[0] == 70,
                "brightness retains latest explicit operation");
    expect_true(service.setting(firefly::CompanionSettingKind::Volume,
                                output) &&
                    output.value[0] == 20,
                "volume revision remains independent");
    expect_true(!service.applySetting(
                    firefly::CompanionSettingKind::Brightness,
                    companion_setting(9, 1000, 19)),
                "brightness below hardware minimum is rejected");

    expect_true(firefly::CompanionSettingsResolver::isNewerRevision(
                    0, UINT32_MAX),
                "uint32 revision wrap follows serial order");
    expect_true(service.applySetting(
                    firefly::CompanionSettingKind::Theme,
                    companion_setting(UINT32_MAX, 900, 1)),
                "theme accepts pre-wrap revision");
    expect_true(service.applySetting(
                    firefly::CompanionSettingKind::Theme,
                    companion_setting(0, 900, 2)),
                "theme accepts wrapped revision at equal timestamp");

    persistence.save_result = false;
    expect_true(!service.applySetting(
                    firefly::CompanionSettingKind::Alarm,
                    companion_setting(1, 1000, 1)),
                "failed persistence reports setting failure");
    expect_true(!service.setting(firefly::CompanionSettingKind::Alarm, output),
                "failed persistence never commits setting state");
}

static uint16_t append_companion_setting_entry(
    firefly::protocol::Frame & frame,
    uint16_t offset,
    firefly::CompanionSettingKind kind,
    const firefly::VersionedCompanionSetting & setting) {
    frame.payload[offset++] = static_cast<uint8_t>(kind);
    frame.payload[offset++] = static_cast<uint8_t>(setting.revision);
    frame.payload[offset++] = static_cast<uint8_t>(setting.revision >> 8);
    frame.payload[offset++] = static_cast<uint8_t>(setting.revision >> 16);
    frame.payload[offset++] = static_cast<uint8_t>(setting.revision >> 24);
    for(uint8_t index = 0; index < 8; ++index) {
        frame.payload[offset++] =
            static_cast<uint8_t>(setting.changed_at >> (index * 8));
    }
    frame.payload[offset++] = static_cast<uint8_t>(setting.value_length);
    frame.payload[offset++] = static_cast<uint8_t>(setting.value_length >> 8);
    memcpy(frame.payload + offset, setting.value, setting.value_length);
    return static_cast<uint16_t>(offset + setting.value_length);
}

static firefly::protocol::Frame companion_settings_frame(
    const firefly::VersionedCompanionSetting & first,
    firefly::CompanionSettingKind first_kind,
    const firefly::VersionedCompanionSetting * second = nullptr,
    firefly::CompanionSettingKind second_kind =
        firefly::CompanionSettingKind::Volume) {
    firefly::protocol::Frame frame{};
    frame.type = firefly::protocol::MessageType::SettingsSet;
    frame.payload[0] = 1;
    frame.payload[1] = second ? 2 : 1;
    uint16_t offset = append_companion_setting_entry(
        frame, 2, first_kind, first
    );
    if(second) {
        offset = append_companion_setting_entry(
            frame, offset, second_kind, *second
        );
    }
    frame.payload_length = offset;
    return frame;
}

static void test_companion_settings_frame_is_atomic() {
    FakeCompanionSettingsPersistence persistence;
    firefly::CompanionSyncService service(&persistence);
    const firefly::VersionedCompanionSetting volume =
        companion_setting(1, 100, 30);
    const firefly::VersionedCompanionSetting invalid_brightness =
        companion_setting(1, 100, 19);
    firefly::protocol::Frame frame = companion_settings_frame(
        volume, firefly::CompanionSettingKind::Volume,
        &invalid_brightness, firefly::CompanionSettingKind::Brightness
    );
    expect_true(!service.applyFrame(frame, 0, 50) &&
                    persistence.calls == 0,
                "later invalid setting rejects frame before persistence");
    firefly::VersionedCompanionSetting output{};
    expect_true(!service.setting(firefly::CompanionSettingKind::Volume,
                                 output),
                "later invalid setting leaves earlier setting unapplied");

    frame = companion_settings_frame(
        volume, firefly::CompanionSettingKind::Volume
    );
    frame.payload[frame.payload_length++] = 0xEE;
    expect_true(!service.applyFrame(frame, 0, 50) &&
                    persistence.calls == 0 &&
                    !service.setting(firefly::CompanionSettingKind::Volume,
                                     output),
                "trailing byte rejects frame with zero partial application");

    persistence.save_result = false;
    frame = companion_settings_frame(
        volume, firefly::CompanionSettingKind::Volume
    );
    expect_true(!service.applyFrame(frame, 0, 50) &&
                    persistence.calls == 1 &&
                    !service.setting(firefly::CompanionSettingKind::Volume,
                                     output),
                "snapshot persistence failure leaves memory unchanged");

    persistence.save_result = true;
    const firefly::VersionedCompanionSetting brightness =
        companion_setting(2, 200, 80);
    frame = companion_settings_frame(
        volume, firefly::CompanionSettingKind::Volume,
        &brightness, firefly::CompanionSettingKind::Brightness
    );
    expect_true(service.applyFrame(frame, 0, 50) &&
                    persistence.calls == 2,
                "valid multi-setting frame persists one atomic snapshot");
    expect_true(service.setting(firefly::CompanionSettingKind::Volume,
                                output) && output.value[0] == 30,
                "atomic frame commits volume");
    expect_true(service.setting(firefly::CompanionSettingKind::Brightness,
                                output) && output.value[0] == 80,
                "atomic frame commits brightness");
}

static void test_companion_local_settings_and_round_trip() {
    FakeCompanionSettingsPersistence persistence;
    firefly::CompanionSyncService service(&persistence);
    const uint8_t brightness_70[] = {70};
    const uint8_t brightness_90[] = {90};
    const uint8_t volume_40[] = {40};
    const char theme_id[] = "firefly-default";
    const firefly::VersionedCompanionSetting alarm =
        companion_alarm_setting(0, 0, 1, 8, "Slot one");
    expect_true(service.recordLocalSetting(
                    firefly::CompanionSettingKind::Brightness,
                    brightness_70, sizeof(brightness_70), 1000),
                "local brightness enters versioned snapshot");
    expect_true(service.recordLocalSetting(
                    firefly::CompanionSettingKind::Volume,
                    volume_40, sizeof(volume_40), 1001),
                "local volume has independent version");
    expect_true(service.recordLocalSetting(
                    firefly::CompanionSettingKind::Brightness,
                    brightness_90, sizeof(brightness_90), 1002),
                "second local brightness increments only brightness");
    expect_true(service.recordLocalSetting(
                    firefly::CompanionSettingKind::Alarm,
                    alarm.value, alarm.value_length, 1003),
                "local alarm records the most recently changed slot");
    expect_true(service.recordLocalSetting(
                    firefly::CompanionSettingKind::Theme,
                    reinterpret_cast<const uint8_t *>(theme_id),
                    strlen(theme_id), 1004),
                "local theme has independent version");

    firefly::VersionedCompanionSetting setting{};
    expect_true(service.setting(firefly::CompanionSettingKind::Brightness,
                                setting) &&
                    setting.revision == 2 && setting.changed_at == 1002 &&
                    setting.value[0] == 90,
                "brightness local revision advances independently");
    expect_true(service.setting(firefly::CompanionSettingKind::Volume,
                                setting) &&
                    setting.revision == 1 && setting.changed_at == 1001 &&
                    setting.value[0] == 40,
                "volume local revision remains independent");
    expect_true(service.setting(firefly::CompanionSettingKind::Alarm,
                                setting) &&
                    setting.revision == 1 && setting.value[0] == 1,
                "alarm local revision remains independent");
    expect_true(service.setting(firefly::CompanionSettingKind::Theme,
                                setting) &&
                    setting.revision == 1 &&
                    strcmp(reinterpret_cast<const char *>(setting.value),
                           theme_id) == 0,
                "theme local revision remains independent");

    persistence.save_result = false;
    const uint8_t volume_50[] = {50};
    expect_true(!service.recordLocalSetting(
                    firefly::CompanionSettingKind::Volume,
                    volume_50, sizeof(volume_50), 1005) &&
                    service.setting(
                        firefly::CompanionSettingKind::Volume, setting) &&
                    setting.revision == 1 && setting.value[0] == 40,
                "failed local snapshot persistence applies nothing");
    persistence.save_result = true;

    firefly::CompanionSyncService rebooted(&persistence);
    expect_true(rebooted.restoreSnapshot(service.settingsSnapshot()) &&
                    rebooted.setting(
                        firefly::CompanionSettingKind::Brightness, setting) &&
                    setting.value[0] == 90,
                "restart restores newest local snapshot");

    firefly::protocol::Frame older = companion_settings_frame(
        companion_setting(99, 999, 20),
        firefly::CompanionSettingKind::Brightness
    );
    expect_true(service.applyFrame(older, 0, 50) &&
                    service.setting(
                        firefly::CompanionSettingKind::Brightness, setting) &&
                    setting.value[0] == 90,
                "newer local operation wins against older remote timestamp");

    firefly::protocol::Frame newer = companion_settings_frame(
        companion_setting(1, 1003, 100),
        firefly::CompanionSettingKind::Brightness
    );
    expect_true(service.applyFrame(newer, 0, 50) &&
                    service.setting(
                        firefly::CompanionSettingKind::Brightness, setting) &&
                    setting.value[0] == 100,
                "newer remote operation wins against local value");

    firefly::protocol::Frame request{};
    request.type = firefly::protocol::MessageType::SettingsGet;
    request.payload[0] = 1;
    request.payload_length = 1;
    expect_true(service.applyFrame(request, 0, 50),
                "settings get schema request is accepted");
    firefly::protocol::Frame response{};
    expect_true(firefly::CompanionSyncService::buildSettingsSnapshot(
                    service.settingsSnapshot(), 77, response) &&
                    response.type ==
                        firefly::protocol::MessageType::SettingsSet &&
                    response.sequence == 77,
                "settings get builds full settings set response");
    firefly::CompanionSyncService round_trip;
    expect_true(round_trip.applyFrame(response, 0, 50) &&
                    round_trip.setting(
                        firefly::CompanionSettingKind::Brightness, setting) &&
                    setting.value[0] == 100 &&
                    round_trip.setting(
                        firefly::CompanionSettingKind::Volume, setting) &&
                    setting.value[0] == 40,
                "settings get response round trips all independent kinds");
}

static void test_companion_alarm_latest_slot_preserves_other_slot() {
    firefly::AlarmService alarms;
    firefly::Alarm slot_zero{};
    slot_zero.configured = true;
    slot_zero.enabled = true;
    slot_zero.hour = 6;
    slot_zero.minute = 15;
    strlcpy(slot_zero.name, "Slot zero", sizeof(slot_zero.name));
    expect_true(alarms.set(0, slot_zero), "alarm slot zero seeded");

    const firefly::VersionedCompanionSetting latest =
        companion_alarm_setting(1, 2000, 1, 8, "Slot one");
    uint8_t decoded_slot = 0;
    firefly::Alarm decoded{};
    expect_true(firefly::CompanionSyncService::decodeAlarm(
                    latest, decoded_slot, decoded) &&
                    decoded_slot == 1 && alarms.set(decoded_slot, decoded),
                "latest alarm setting decodes its explicit slot");
    expect_true(alarms.get(0).hour == 6 &&
                    strcmp(alarms.get(0).name, "Slot zero") == 0,
                "applying slot one never destroys slot zero");
    expect_true(alarms.get(1).hour == 8 &&
                    strcmp(alarms.get(1).name, "Slot one") == 0,
                "latest alarm operation applies only named slot");
}

static void test_music_target_is_explicit_and_local_first() {
    firefly::MusicControlSelector selector;
    expect_true(selector.target() ==
                    firefly::MusicControlTarget::LocalLibrary,
                "music target defaults to local library");
    selector.toggle();
    expect_true(selector.target() ==
                    firefly::MusicControlTarget::PhoneRemote,
                "long refresh explicitly selects phone remote");
    selector.noteScanCompleted(0);
    expect_true(selector.target() ==
                    firefly::MusicControlTarget::PhoneRemote,
                "empty scan never changes explicit phone target");
    selector.noteScanCompleted(12);
    expect_true(selector.target() ==
                    firefly::MusicControlTarget::PhoneRemote,
                "nonempty scan never changes explicit phone target");
    selector.noteLocalTrackSelected();
    expect_true(selector.target() ==
                    firefly::MusicControlTarget::LocalLibrary,
                "selecting local track forces local target");
}

static bool music_local_volume_save_result = false;
static uint8_t music_local_volume_requested = 0;

static bool save_music_local_volume(uint8_t volume) {
    music_local_volume_requested = volume;
    return music_local_volume_save_result;
}

static void test_music_empty_local_transport_and_volume_commit() {
    uint16_t target = 99;
    expect_true(
        !firefly::MusicQueueNavigator::play(0, 0, target) && target == 99,
        "empty local queue play is a safe no-op"
    );
    expect_true(
        !firefly::MusicQueueNavigator::previous(0, 0, target) && target == 99,
        "empty local queue previous avoids underflow"
    );
    expect_true(
        !firefly::MusicQueueNavigator::next(0, 0, target) && target == 99,
        "empty local queue next avoids modulo zero"
    );
    expect_true(
        firefly::MusicQueueNavigator::previous(0, 3, target) && target == 2 &&
        firefly::MusicQueueNavigator::next(2, 3, target) && target == 0,
        "nonempty local queue navigation still wraps"
    );

    firefly::MusicApp app;
    app.setLocalVolumeCallback(save_music_local_volume, 50);
    music_local_volume_save_result = false;
    expect_true(!app.applyLocalVolume(75) &&
                    music_local_volume_requested == 75 &&
                    app.localVolume() == 50,
                "failed local volume persistence never advances music state");
    music_local_volume_save_result = true;
    expect_true(app.applyLocalVolume(75) && app.localVolume() == 75,
                "persisted local volume advances music state");
}

static void write_companion_i16(uint8_t * output, int16_t value) {
    output[0] = static_cast<uint8_t>(value & 0xFF);
    output[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

static void write_companion_i64(uint8_t * output, int64_t value) {
    const uint64_t raw = static_cast<uint64_t>(value);
    for(uint8_t i = 0; i < 8; ++i) {
        output[i] = static_cast<uint8_t>((raw >> (i * 8)) & 0xFF);
    }
}

static firefly::protocol::Frame companion_weather_frame(int64_t updated_at) {
    firefly::protocol::Frame frame{};
    frame.type = firefly::protocol::MessageType::WeatherUpdate;
    frame.payload[0] = 1;
    frame.payload[1] = 6;
    frame.payload[2] = 61;
    frame.payload[3] = 0;
    write_companion_i16(frame.payload + 4, 267);
    write_companion_i16(frame.payload + 6, 301);
    write_companion_i16(frame.payload + 8, 224);
    write_companion_i64(frame.payload + 10, updated_at);
    memcpy(frame.payload + 18, "Paris!", 6);
    frame.payload_length = 24;
    return frame;
}

static void test_companion_weather_cache_boundaries_and_disconnect() {
    firefly::CompanionSyncService service;
    const int64_t updated = 1000000;
    firefly::protocol::Frame frame = companion_weather_frame(updated);
    expect_true(service.applyFrame(frame, 100, 50),
                "companion accepts bounded weather payload");

    firefly::CompanionWeather weather{};
    expect_true(service.weatherAt(updated + 3 * 60 * 60, false, weather) ==
                    firefly::WeatherFreshness::Fresh,
                "weather is fresh at exactly three hours while disconnected");
    expect_true(service.weatherAt(updated + 3 * 60 * 60 + 1, false, weather) ==
                    firefly::WeatherFreshness::Stale,
                "weather is stale after three hours");
    expect_true(service.weatherAt(updated + 24 * 60 * 60, false, weather) ==
                    firefly::WeatherFreshness::Stale,
                "weather remains readable at exactly twenty four hours");
    expect_true(service.weatherAt(updated + 24 * 60 * 60 + 1, false, weather) ==
                    firefly::WeatherFreshness::Expired &&
                    !weather.valid,
                "weather expires only after twenty four hours");

    frame = companion_weather_frame(updated);
    frame.payload[18] = 0xC0;
    expect_true(!service.applyFrame(frame, 100, 50),
                "weather validates UTF-8 before fixed-buffer copy");
}

static void test_companion_weather_presenter_states() {
    firefly::CompanionWeather weather{};
    weather.valid = true;
    strlcpy(weather.city, "Paris", sizeof(weather.city));
    weather.temperature_tenths_c = 267;
    weather.high_tenths_c = 301;
    weather.low_tenths_c = 224;
    weather.weather_code = 61;
    weather.updated_at_epoch = 1000;

    firefly::CompanionWeatherView view =
        firefly::CompanionWeatherPresenter::build(
            weather, firefly::WeatherFreshness::Fresh, true
        );
    expect_true(strcmp(view.city, "Paris") == 0 &&
                    strstr(view.current, "26.7") != nullptr &&
                    strstr(view.range, "30.1") != nullptr &&
                    strstr(view.range, "22.4") != nullptr &&
                    strstr(view.code, "61") != nullptr &&
                    strstr(view.status, "fresh") != nullptr,
                "fresh weather view exposes all synced fields");

    view = firefly::CompanionWeatherPresenter::build(
        weather, firefly::WeatherFreshness::Stale, false
    );
    expect_true(strstr(view.status, "stale") != nullptr &&
                    strstr(view.status, "offline") != nullptr &&
                    strcmp(view.city, "Paris") == 0,
                "stale offline view keeps cached weather");

    view = firefly::CompanionWeatherPresenter::build(
        firefly::CompanionWeather{},
        firefly::WeatherFreshness::Expired,
        false
    );
    expect_true(strstr(view.status, "expired") != nullptr &&
                    strstr(view.status, "unavailable") != nullptr,
                "expired weather is explicitly unavailable");
}

static firefly::protocol::Frame companion_calendar_frame(uint8_t count) {
    firefly::protocol::Frame frame{};
    frame.type = firefly::protocol::MessageType::CalendarUpdate;
    frame.payload[0] = 1;
    frame.payload[1] = 1;
    frame.payload[2] = count;
    write_companion_i64(frame.payload + 3, 2000000);
    uint16_t offset = 11;
    for(uint8_t i = 0; i < count && i < 8; ++i) {
        frame.payload[offset++] = 1;
        write_companion_i64(frame.payload + offset, 2100000 + i * 1000);
        offset += 8;
        write_companion_i64(frame.payload + offset, 2100500 + i * 1000);
        offset += 8;
        frame.payload[offset++] = i == 0 ? 1 : 0;
        frame.payload[offset++] = static_cast<uint8_t>('A' + i);
    }
    frame.payload_length = offset;
    return frame;
}

static void test_companion_calendar_is_bounded_and_disable_preserves_local_date() {
    firefly::CompanionSyncService service;
    firefly::protocol::Frame frame = companion_calendar_frame(8);
    expect_true(service.applyFrame(frame, 100, 50) &&
                    service.calendar().enabled &&
                    service.calendar().count == 8,
                "calendar accepts at most eight whitelisted summaries");
    frame = companion_calendar_frame(9);
    expect_true(!service.applyFrame(frame, 100, 50),
                "calendar rejects over-bound entry count");

    frame = companion_calendar_frame(0);
    frame.payload[1] = 0;
    expect_true(service.applyFrame(frame, 100, 50) &&
                    !service.calendar().enabled &&
                    service.calendar().count == 0,
                "permission denial disables only synced agenda");
}

static void test_find_watch_policy_duration_low_battery_and_cancel() {
    const firefly::FindWatchPlan normal =
        firefly::FindDevicePolicy::watchPlan(50);
    expect_true(normal.duration_ms == 30000 && normal.play_audio &&
                    normal.flash_screen,
                "normal find watch runs sound and light for thirty seconds");
    const firefly::FindWatchPlan low =
        firefly::FindDevicePolicy::watchPlan(5);
    expect_true(low.duration_ms == 5000 && !low.play_audio &&
                    low.flash_screen,
                "extremely low battery uses five second silent flash");

    firefly::CompanionSyncService service;
    firefly::protocol::Frame frame{};
    frame.type = firefly::protocol::MessageType::FindWatch;
    frame.payload[0] = 1;
    frame.payload[1] = 1;
    frame.payload_length = 2;
    expect_true(service.applyFrame(frame, 100, 50) &&
                    service.findWatchAt(30099).active,
                "find watch remains active before deadline");
    expect_true(!service.findWatchAt(30100).active,
                "find watch expires at thirty second deadline");
    expect_true(service.applyFrame(frame, 40000, 50) &&
                    service.cancelFindWatch() &&
                    !service.findWatchAt(40001).active,
                "find watch supports physical-path cancellation");
}

static void test_companion_dispatcher_routes_notifications_and_business_frames() {
    firefly::NotificationService notifications;
    firefly::CompanionSyncService companion;
    firefly::CompanionFrameDispatcher dispatcher(notifications, companion);

    firefly::protocol::Frame dismiss{};
    dismiss.type = firefly::protocol::MessageType::NotificationDismiss;
    dismiss.payload[0] = 1;
    dismiss.payload[1] = 0;
    dismiss.payload[2] = 0;
    dismiss.payload_length = 3;
    expect_true(dispatcher.dispatch(dismiss, 100, 50) ==
                    firefly::CompanionDispatchResult::Notification,
                "main-loop dispatcher routes notification frames");

    firefly::protocol::Frame weather = companion_weather_frame(1000);
    expect_true(dispatcher.dispatch(weather, 100, 50) ==
                    firefly::CompanionDispatchResult::Companion,
                "main-loop dispatcher also routes companion frames");
}

static void test_companion_remote_error_decode_and_text() {
    firefly::protocol::Frame frame{};
    frame.type = firefly::protocol::MessageType::Error;
    frame.payload[0] = 1;
    frame.payload[1] = static_cast<uint8_t>(
        firefly::protocol::MessageType::MediaCommand
    );
    frame.payload[2] = 2;
    frame.payload_length = 3;
    firefly::CompanionRemoteError error{};
    expect_true(firefly::CompanionSyncService::decodeError(frame, error) &&
                    error.failed_type ==
                        firefly::protocol::MessageType::MediaCommand &&
                    error.code ==
                        firefly::protocol::WireErrorCode::NoActiveMediaSession,
                "watch decodes explicit phone media error");
    expect_true(strcmp(
                    firefly::CompanionSyncService::remoteErrorText(error),
                    "NO MEDIA") == 0,
                "watch maps phone media error to bounded status text");
}

static void test_ble_frame_codec_golden_frames() {
    using namespace firefly::protocol;
    static const uint8_t hello_golden[] = {
        0x46, 0x46, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x4D, 0xE0
    };
    Frame hello{};
    hello.type = MessageType::Hello;
    hello.sequence = 1;
    uint8_t encoded[kMaxEncodedFrame]{};
    const size_t hello_length = FrameCodec::encode(hello, encoded, sizeof(encoded));
    expect_true(hello_length == sizeof(hello_golden), "BLE Hello encoded length");
    expect_true(memcmp(encoded, hello_golden, sizeof(hello_golden)) == 0,
                "BLE Hello golden bytes");

    static const uint8_t notification_golden[] = {
        0x46, 0x46, 0x01, 0x20, 0x01, 0x34, 0x12, 0x06, 0x00,
        0xE5, 0x25, 0xE6, 0x9D, 0xA5, 0xE7, 0x94, 0xB5
    };
    Frame notification{};
    notification.type = MessageType::NotificationPush;
    notification.flags = FrameFlag::AckRequired;
    notification.sequence = 0x1234;
    notification.payload_length = 6;
    memcpy(notification.payload, notification_golden + kHeaderSize,
           notification.payload_length);
    const size_t notification_length =
        FrameCodec::encode(notification, encoded, sizeof(encoded));
    expect_true(notification_length == sizeof(notification_golden),
                "BLE notification encoded length");
    expect_true(memcmp(encoded, notification_golden, sizeof(notification_golden)) == 0,
                "BLE notification golden bytes");

    Frame decoded{};
    expect_true(FrameCodec::decode(notification_golden,
                                   sizeof(notification_golden), decoded) ==
                    DecodeError::None,
                "BLE notification decodes");
    expect_true(decoded.type == MessageType::NotificationPush &&
                    decoded.flags == FrameFlag::AckRequired &&
                    decoded.sequence == 0x1234 && decoded.payload_length == 6 &&
                    memcmp(decoded.payload, notification.payload, 6) == 0,
                "BLE notification fields round trip");

    uint8_t corrupted[sizeof(notification_golden)]{};
    memcpy(corrupted, notification_golden, sizeof(corrupted));
    corrupted[sizeof(corrupted) - 1] ^= 0x01;
    expect_true(FrameCodec::decode(corrupted, sizeof(corrupted), decoded) ==
                    DecodeError::CrcMismatch,
                "BLE corrupt CRC rejected");

    notification.payload_length = kMaxPayload + 1;
    expect_true(FrameCodec::encode(notification, encoded, sizeof(encoded)) == 0,
                "BLE oversized payload rejected before copy");
}

class FakeBlePeripheral final : public firefly::BlePeripheralTransport {
public:
    bool begin(const char *, ReceiveCallback callback) override {
        callback_ = callback;
        return true;
    }

    void advertise(uint16_t interval_ms) override {
        last_advertising_interval_ms = interval_ms;
        ++advertise_count;
    }

    void stopAdvertising() override { ++stop_advertising_count; }

    bool notify(const uint8_t * data, size_t length) override {
        if(!connected_value || !data || length > sizeof(last_notification) ||
           length > firefly::protocol::attChunkLimit(mtu)) {
            return false;
        }
        memcpy(last_notification, data, length);
        last_notification_length = length;
        ++notify_count;
        return true;
    }

    bool connected() const override { return connected_value; }
    uint16_t negotiatedMtu() const override { return mtu; }
    void disconnect() override { connected_value = false; }
    void setThroughputMode(bool enabled) override {
        throughput_mode = enabled;
        ++throughput_change_count;
    }

    void setSecurityCallback(SecurityCallback callback) override {
        security_callback = callback;
    }

    void authorizePairing(bool authorized) override {
        pairing_authorized = authorized;
    }

    bool requestSecureBond(uint32_t passkey) override {
        requested_passkey = passkey;
        ++secure_bond_requests;
        return connected_value && pairing_authorized;
    }

    bool requestEncryptedLink() override {
        ++encrypted_link_requests;
        return connected_value;
    }

    bool encrypted() const override { return encrypted_value; }

    bool clearBonds() override {
        ++clear_bond_count;
        encrypted_value = false;
        return true;
    }

    void triggerSecurityResult(bool success) {
        encrypted_value = success;
        if(security_callback) security_callback(success);
    }

    ReceiveCallback callback_ = nullptr;
    uint8_t last_notification[firefly::protocol::kMaxAttChunk]{};
    size_t last_notification_length = 0;
    uint16_t mtu = 185;
    uint16_t last_advertising_interval_ms = 0;
    uint8_t advertise_count = 0;
    uint8_t stop_advertising_count = 0;
    uint8_t notify_count = 0;
    uint8_t throughput_change_count = 0;
    bool connected_value = false;
    bool throughput_mode = false;
    bool pairing_authorized = false;
    bool encrypted_value = false;
    SecurityCallback security_callback = nullptr;
    uint32_t requested_passkey = 0;
    uint8_t secure_bond_requests = 0;
    uint8_t encrypted_link_requests = 0;
    uint8_t clear_bond_count = 0;
};

class FakePairingStore final : public firefly::PairingStore {
public:
    bool loadPairing(firefly::PairingRecord & output) override {
        output = record;
        return true;
    }

    bool savePairing(const firefly::PairingRecord & value) override {
        ++save_count;
        if(save_result) record = value;
        return save_result;
    }

    bool clearPairing() override {
        ++clear_count;
        if(clear_result) record = {};
        return clear_result;
    }

    firefly::PairingRecord record{};
    uint8_t save_count = 0;
    uint8_t clear_count = 0;
    bool save_result = true;
    bool clear_result = true;
};

static void fill_test_random(uint8_t * output, size_t length) {
    for(size_t i = 0; i < length; ++i) output[i] = static_cast<uint8_t>(i + 1U);
}

static void drain_events(firefly::EventBus & events) {
    firefly::SystemEvent event{};
    while(events.take(event)) {}
}

static void test_connectivity_service_bounded_flow() {
    using namespace firefly;
    using namespace firefly::protocol;
    FakeBlePeripheral transport;
    EventBus events;
    StateStore state;
    FakePairingStore pairing_store;
    ConnectivityService connectivity(transport, events, state, pairing_store);

    expect_true(connectivity.begin("FireflyOS Test", false, 0),
                "BLE connectivity begins");
    expect_true(transport.last_advertising_interval_ms ==
                    ConnectivityService::kFastAdvertisingIntervalMs,
                "BLE begins fast advertising");
    connectivity.service(ConnectivityService::kUnpairedFastAdvertisingMs);
    expect_true(transport.last_advertising_interval_ms ==
                    ConnectivityService::kSlowAdvertisingIntervalMs,
                "BLE unpaired advertising slows after sixty seconds");
    BatteryState battery{};
    battery.percent = 61;
    battery.valid = true;
    state.setBattery(battery);
    transport.connected_value = true;
    connectivity.service(91000);
    expect_true(state.snapshot().phone_connected,
                "BLE connection updates phone state");
    expect_true(transport.throughput_mode,
                "BLE connection starts in throughput mode");
    drain_events(events);

    Frame hello{};
    hello.type = MessageType::Hello;
    hello.sequence = 1;
    uint8_t encoded[kMaxEncodedFrame]{};
    size_t encoded_length = FrameCodec::encode(hello, encoded, sizeof(encoded));
    expect_true(connectivity.enqueueReceived(encoded, encoded_length),
                "BLE queues encoded Hello");
    connectivity.service(91001);
    Frame received{};
    expect_true(connectivity.takeReceivedFrame(received) &&
                    received.type == MessageType::Hello && received.sequence == 1,
                "BLE publishes decoded Hello through event boundary");
    drain_events(events);

    expect_true(connectivity.enqueueReceived(encoded, encoded_length),
                "BLE queues duplicate Hello");
    connectivity.service(91002);
    expect_true(!connectivity.takeReceivedFrame(received),
                "BLE duplicate sequence is not dispatched twice");
    bool duplicate_error = false;
    SystemEvent event{};
    while(events.take(event)) {
        duplicate_error = duplicate_error ||
            (event.type == EventType::BleProtocolError &&
             event.value == static_cast<uint32_t>(ConnectivityError::DuplicateSequence));
    }
    expect_true(duplicate_error, "BLE duplicate sequence reports protocol error");

    static const uint8_t logical_payload[] = {
        0xE8, 0x90, 0xA4, 0xE7, 0x81, 0xAB, 0xE8, 0x99, 0xAB
    };
    for(uint8_t index = 0; index < 3; ++index) {
        Frame fragment{};
        fragment.type = MessageType::Hello;
        fragment.flags = static_cast<uint8_t>(
            FrameFlag::Fragment |
            (index == 2 ? FrameFlag::LastFragment : 0)
        );
        fragment.sequence = 2;
        fragment.payload_length = 5;
        fragment.payload[0] = index;
        fragment.payload[1] = 3;
        memcpy(fragment.payload + 2, logical_payload + index * 3, 3);
        encoded_length = FrameCodec::encode(fragment, encoded, sizeof(encoded));
        expect_true(connectivity.enqueueReceived(encoded, encoded_length),
                    "BLE queues ordered fragment");
    }
    connectivity.service(91003);
    expect_true(connectivity.takeReceivedFrame(received) &&
                    received.type == MessageType::Hello &&
                    received.sequence == 2 && received.payload_length == 9 &&
                    memcmp(received.payload, logical_payload, 9) == 0,
                "BLE reassembles three fragments before dispatch");
    drain_events(events);

    Frame queued_first{};
    queued_first.type = MessageType::Hello;
    queued_first.sequence = 3;
    encoded_length = FrameCodec::encode(queued_first, encoded, sizeof(encoded));
    expect_true(connectivity.enqueueReceived(encoded, encoded_length),
                "BLE queues first business frame");
    Frame queued_second = queued_first;
    queued_second.sequence = 4;
    encoded_length = FrameCodec::encode(queued_second, encoded, sizeof(encoded));
    expect_true(connectivity.enqueueReceived(encoded, encoded_length),
                "BLE queues second business frame");
    connectivity.service(91004);
    Frame first_received{};
    Frame second_received{};
    expect_true(connectivity.takeReceivedFrame(first_received) &&
                    connectivity.takeReceivedFrame(second_received) &&
                    first_received.sequence == 3 &&
                    second_received.sequence == 4,
                "BLE main-loop mailbox preserves consecutive business frames");
    drain_events(events);

    for(uint16_t sequence = 10;
        sequence < 10 + ConnectivityService::kDispatchQueueCapacity;
        ++sequence) {
        Frame fill{};
        fill.type = MessageType::Hello;
        fill.sequence = sequence;
        encoded_length = FrameCodec::encode(fill, encoded, sizeof(encoded));
        expect_true(connectivity.enqueueReceived(encoded, encoded_length),
                    "BLE accepts frame used to fill dispatch queue");
        connectivity.service(91100 + sequence);
    }
    const uint8_t acknowledgements_before_full = transport.notify_count;
    Frame rejected{};
    rejected.type = MessageType::Hello;
    rejected.sequence = 14;
    encoded_length = FrameCodec::encode(rejected, encoded, sizeof(encoded));
    expect_true(connectivity.enqueueReceived(encoded, encoded_length),
                "BLE accepts retry candidate into receive queue");
    connectivity.service(91114);
    expect_true(transport.notify_count == acknowledgements_before_full,
                "dispatch rejection is not acknowledged");
    expect_true(connectivity.takeReceivedFrame(received),
                "main loop frees one dispatch slot");
    expect_true(connectivity.enqueueReceived(encoded, encoded_length),
                "same sequence can retry after failed publish");
    connectivity.service(91115);
    expect_true(transport.notify_count == acknowledgements_before_full + 1,
                "successful retry advances sequence and receives ACK");
    while(connectivity.takeReceivedFrame(received)) {}
    drain_events(events);

    Frame outbound{};
    outbound.type = MessageType::Hello;
    outbound.flags = FrameFlag::AckRequired;
    outbound.sequence = 9;
    outbound.payload_length = 16;
    for(uint8_t index = 0; index < outbound.payload_length; ++index) {
        outbound.payload[index] = index;
    }
    transport.mtu = 23;
    const uint8_t notify_count_before_retry = transport.notify_count;
    expect_true(connectivity.send(outbound, 92000),
                "BLE fragments ACK-required frame to negotiated MTU");
    expect_true(transport.notify_count == notify_count_before_retry + 3 &&
                    transport.last_notification_length <=
                        protocol::attChunkLimit(transport.mtu),
                "BLE sends every outbound fragment within ATT limit");
    connectivity.service(94000);
    connectivity.service(96000);
    connectivity.service(98000);
    expect_true(transport.notify_count == notify_count_before_retry + 12,
                "BLE retries complete ACK-required fragment batch three times");
    connectivity.service(100000);
    expect_true(transport.notify_count == notify_count_before_retry + 12,
                "BLE stops fragment batches after retry budget");
    bool timeout_error = false;
    while(events.take(event)) {
        timeout_error = timeout_error ||
            (event.type == EventType::BleProtocolError &&
             event.value == static_cast<uint32_t>(ConnectivityError::AckTimeout));
    }
    expect_true(timeout_error, "BLE ACK timeout is observable");

    transport.connected_value = false;
    connectivity.service(100001);
    const SystemState disconnected = state.snapshot();
    expect_true(!disconnected.phone_connected,
                "BLE disconnect clears only phone connection state");
    expect_true(disconnected.battery.valid && disconnected.battery.percent == 61,
                "BLE disconnect preserves unrelated cached state");
}

static void test_ble_message_authenticator() {
    using namespace firefly::protocol;
    using firefly::MessageAuthenticator;
    uint8_t token[MessageAuthenticator::kAppTokenSize]{};
    for(uint8_t i = 0; i < sizeof(token); ++i) token[i] = i;

    Frame frame{};
    frame.type = MessageType::SettingsSet;
    frame.flags = FrameFlag::AckRequired;
    frame.sequence = 0x1234;
    frame.payload_length = 3;
    frame.payload[0] = 0x10;
    frame.payload[1] = 0x20;
    frame.payload[2] = 0x30;
    expect_true(MessageAuthenticator::appendTag(frame, token),
                "BLE HMAC tag appends to sensitive frame");
    expect_true(frame.payload_length == 3 + MessageAuthenticator::kAuthTagSize,
                "BLE HMAC tag is truncated to eight bytes");
    static const uint8_t hmac_golden[MessageAuthenticator::kAuthTagSize] = {
        0x20, 0xD1, 0x8D, 0x97, 0x0E, 0xF3, 0x13, 0x36
    };
    expect_true(memcmp(frame.payload + 3, hmac_golden,
                       sizeof(hmac_golden)) == 0,
                "BLE HMAC matches SHA256 golden tag");

    Frame verified = frame;
    expect_true(MessageAuthenticator::verifyAndStrip(verified, token),
                "BLE correct token authenticates");
    expect_true(verified.payload_length == 3 &&
                    verified.payload[0] == 0x10 &&
                    verified.payload[2] == 0x30,
                "BLE authentication strips only tag");

    frame.payload[frame.payload_length - 1] ^= 0x01;
    expect_true(!MessageAuthenticator::verifyAndStrip(frame, token),
                "BLE wrong authentication tag is rejected");
}

static void test_connectivity_pairing_and_security() {
    using namespace firefly;
    using namespace firefly::protocol;

    {
        FakeBlePeripheral transport;
        EventBus events;
        StateStore state;
        FakePairingStore store;
        ConnectivityService connectivity(transport, events, state, store);
        connectivity.setRandomBytesCallback(&fill_test_random);
        expect_true(connectivity.begin("FireflyOS Test", false, 0),
                    "BLE unpaired service begins");
        transport.connected_value = true;
        connectivity.service(1);
        drain_events(events);

        Frame request{};
        request.type = MessageType::PairRequest;
        request.sequence = 1;
        const char phone_name[] = "Pixel 9";
        request.payload_length = sizeof(phone_name) - 1;
        memcpy(request.payload, phone_name, request.payload_length);
        uint8_t encoded[kMaxEncodedFrame]{};
        const size_t length = FrameCodec::encode(request, encoded, sizeof(encoded));
        expect_true(connectivity.enqueueReceived(encoded, length),
                    "BLE pair request queues");
        connectivity.service(2);
        const PairingSnapshot pending = connectivity.pairingSnapshot();
        expect_true(pending.state == PairingState::AwaitingUser &&
                        pending.passkey >= 100000 && pending.passkey <= 999999 &&
                        strcmp(pending.phone_name, phone_name) == 0,
                    "BLE pair request exposes bounded user confirmation");
        expect_true(connectivity.confirmPairing(true, 3) &&
                        transport.secure_bond_requests == 1 &&
                        transport.requested_passkey == pending.passkey,
                    "BLE user approval starts secure bond");

        transport.triggerSecurityResult(true);
        connectivity.service(4);
        expect_true(store.record.valid && !store.record.confirmed &&
                        store.save_count == 1,
                    "BLE successful bond persists provisional app token");
        expect_true(strcmp(store.record.phone_name, phone_name) == 0,
                    "BLE successful bond persists phone name");
        expect_true(!connectivity.paired() &&
                        connectivity.pairingSnapshot().state ==
                            PairingState::AwaitingPairConfirmAck,
                    "BLE waits for PairConfirm ACK before marking paired");
        Frame pair_confirmation{};
        expect_true(FrameCodec::decode(
                        transport.last_notification,
                        transport.last_notification_length,
                        pair_confirmation) == DecodeError::None &&
                        pair_confirmation.type == MessageType::PairConfirm,
                    "BLE publishes PairConfirm transaction");
        Frame pair_ack{};
        pair_ack.type = MessageType::Ack;
        pair_ack.flags = FrameFlag::IsAck;
        pair_ack.sequence = pair_confirmation.sequence;
        const size_t pair_ack_length =
            FrameCodec::encode(pair_ack, encoded, sizeof(encoded));
        connectivity.enqueueReceived(encoded, pair_ack_length);
        connectivity.service(5);
        expect_true(connectivity.paired() && store.record.confirmed &&
                        store.save_count == 2 &&
                        connectivity.pairingSnapshot().state ==
                            PairingState::Paired,
                    "BLE commits pairing record only after PairConfirm ACK");
    }

    {
        FakeBlePeripheral transport;
        EventBus events;
        StateStore state;
        FakePairingStore store;
        store.record.valid = true;
        store.record.confirmed = false;
        strlcpy(store.record.phone_name, "Interrupted",
                sizeof(store.record.phone_name));
        ConnectivityService connectivity(transport, events, state, store);
        expect_true(connectivity.begin("FireflyOS Test", true, 0) &&
                        !connectivity.paired() &&
                        store.clear_count == 1 &&
                        transport.clear_bond_count == 1 &&
                        !store.record.valid,
                    "BLE boot rolls back provisional pairing record");
    }

    {
        FakeBlePeripheral transport;
        EventBus events;
        StateStore state;
        FakePairingStore store;
        store.record.valid = true;
        store.record.confirmed = true;
        strlcpy(store.record.phone_name, "Pixel 9", sizeof(store.record.phone_name));
        for(uint8_t i = 0; i < sizeof(store.record.app_token); ++i) {
            store.record.app_token[i] = static_cast<uint8_t>(0xA0 + i);
        }
        ConnectivityService connectivity(transport, events, state, store);
        expect_true(connectivity.begin("FireflyOS Test", true, 0),
                    "BLE paired service begins");
        connectivity.service(ConnectivityService::kPairedFastAdvertisingMs);
        expect_true(transport.last_advertising_interval_ms ==
                        ConnectivityService::kSlowAdvertisingIntervalMs,
                    "BLE paired reconnect window lasts twenty seconds");
        transport.connected_value = true;
        transport.encrypted_value = true;
        connectivity.service(1);
        drain_events(events);

        Frame command{};
        command.type = MessageType::SettingsSet;
        command.sequence = 10;
        command.payload_length = 1;
        command.payload[0] = 0x42;
        expect_true(MessageAuthenticator::appendTag(command, store.record.app_token),
                    "BLE signed settings command builds");
        uint8_t encoded[kMaxEncodedFrame]{};
        const size_t length = FrameCodec::encode(command, encoded, sizeof(encoded));
        connectivity.enqueueReceived(encoded, length);
        connectivity.service(2);
        Frame received{};
        expect_true(connectivity.takeReceivedFrame(received) &&
                        received.type == MessageType::SettingsSet &&
                        received.payload_length == 1,
                    "BLE authenticated settings command dispatches");
        drain_events(events);

        connectivity.enqueueReceived(encoded, length);
        connectivity.service(3);
        expect_true(!connectivity.takeReceivedFrame(received),
                    "BLE replayed authenticated sequence is rejected");
        drain_events(events);

        const uint8_t acknowledgements_before_bad_duplicate =
            transport.notify_count;
        Frame bad_duplicate = command;
        bad_duplicate.payload[bad_duplicate.payload_length - 1] ^= 0x01;
        const size_t bad_duplicate_length =
            FrameCodec::encode(bad_duplicate, encoded, sizeof(encoded));
        connectivity.enqueueReceived(encoded, bad_duplicate_length);
        connectivity.service(4);
        Frame unauthorized_response{};
        expect_true(
            transport.notify_count ==
                acknowledgements_before_bad_duplicate + 1 &&
                FrameCodec::decode(
                    transport.last_notification,
                    transport.last_notification_length,
                    unauthorized_response) == DecodeError::None &&
                unauthorized_response.type == MessageType::Error &&
                unauthorized_response.payload_length == 3 &&
                unauthorized_response.payload[2] ==
                    static_cast<uint8_t>(
                        protocol::WireErrorCode::Unauthorized) &&
                !connectivity.takeReceivedFrame(received),
            "BLE bad-HMAC duplicate returns Unauthorized without dispatch"
        );
        bool bad_duplicate_unauthorized = false;
        SystemEvent bad_duplicate_event{};
        while(events.take(bad_duplicate_event)) {
            bad_duplicate_unauthorized = bad_duplicate_unauthorized ||
                (bad_duplicate_event.type == EventType::BleProtocolError &&
                 bad_duplicate_event.value ==
                    static_cast<uint32_t>(ConnectivityError::Unauthorized));
        }
        expect_true(bad_duplicate_unauthorized,
                    "BLE bad-HMAC duplicate counts as authentication failure");

        Frame wrong = command;
        wrong.sequence = 11;
        wrong.payload[wrong.payload_length - 1] ^= 0x01;
        const size_t wrong_length = FrameCodec::encode(wrong, encoded, sizeof(encoded));
        for(uint8_t attempt = 0; attempt < ConnectivityService::kMaxAuthenticationFailures;
            ++attempt) {
            connectivity.enqueueReceived(encoded, wrong_length);
            connectivity.service(10 + attempt);
        }
        expect_true(!transport.connected_value,
                    "BLE five authentication failures disconnect peer");
    }

    {
        FakeBlePeripheral transport;
        EventBus events;
        StateStore state;
        FakePairingStore store;
        store.record.valid = true;
        store.record.confirmed = true;
        strlcpy(store.record.phone_name, "Pixel 9", sizeof(store.record.phone_name));
        ConnectivityService connectivity(transport, events, state, store);
        connectivity.begin("FireflyOS Test", true, 0);
        transport.connected_value = true;
        transport.encrypted_value = true;
        connectivity.service(1);
        drain_events(events);
        expect_true(connectivity.requestUnpairConfirmation(2),
                    "BLE watch can request unpair confirmation");
        expect_true(connectivity.confirmUnpair(true, 3),
                    "BLE confirmed unpair sends confirmation transaction");
        expect_true(store.clear_count == 0 && store.record.valid &&
                        transport.clear_bond_count == 0 &&
                        transport.connected_value &&
                        connectivity.pairingSnapshot().state ==
                            PairingState::AwaitingUnpairAck,
                    "BLE keeps binding until UnpairConfirm ACK");
        Frame unpair_confirmation{};
        expect_true(FrameCodec::decode(
                        transport.last_notification,
                        transport.last_notification_length,
                        unpair_confirmation) == DecodeError::None &&
                        unpair_confirmation.type == MessageType::UnpairConfirm,
                    "BLE publishes UnpairConfirm transaction");
        Frame unpair_ack{};
        unpair_ack.type = MessageType::Ack;
        unpair_ack.flags = FrameFlag::IsAck;
        unpair_ack.sequence = unpair_confirmation.sequence;
        uint8_t unpair_ack_bytes[kMaxEncodedFrame]{};
        const size_t unpair_ack_length = FrameCodec::encode(
            unpair_ack, unpair_ack_bytes, sizeof(unpair_ack_bytes)
        );
        connectivity.enqueueReceived(unpair_ack_bytes, unpair_ack_length);
        connectivity.service(4);
        expect_true(store.clear_count == 1 && !store.record.valid &&
                        transport.clear_bond_count == 1 &&
                        !transport.connected_value,
                    "BLE unpair commits token bond and disconnect after ACK");
    }
}

static void test_event_bus_fifo() {
    firefly::EventBus bus;
    firefly::SystemEvent first{firefly::EventType::ShortPress, 1, 10};
    firefly::SystemEvent second{firefly::EventType::EnterSleep, 2, 20};
    expect_true(bus.post(first), "post first event");
    expect_true(bus.post(second), "post second event");
    firefly::SystemEvent out{};
    expect_true(bus.take(out) && out.type == first.type && out.value == 1,
                "event FIFO first");
    expect_true(bus.take(out) && out.type == second.type && out.value == 2,
                "event FIFO second");
    expect_true(!bus.take(out), "empty queue returns false");
}

static void test_event_bus_full_policy() {
    firefly::EventBus bus;
    for(uint8_t i = 0; i < firefly::EventBus::kCapacity; ++i) {
        const firefly::EventPriority priority = i == 3
            ? firefly::EventPriority::Refresh
            : firefly::EventPriority::Normal;
        expect_true(
            bus.post({firefly::EventType::BatteryChanged, i, i, priority}),
            "fill event bus"
        );
    }
    expect_true(
        !bus.post({firefly::EventType::TimeChanged, 99, 99}),
        "normal event rejected when full"
    );
    expect_true(
        bus.post({firefly::EventType::AlarmTriggered, 7, 100,
                  firefly::EventPriority::Critical}),
        "critical event replaces refresh"
    );
    expect_true(bus.size() == firefly::EventBus::kCapacity,
                "replacement keeps capacity");

    bool found_critical = false;
    firefly::SystemEvent out{};
    while(bus.take(out)) {
        found_critical = found_critical || out.type == firefly::EventType::AlarmTriggered;
    }
    expect_true(found_critical, "critical event remains queued");
}

static void test_event_bus_preserves_full_critical_queue() {
    firefly::EventBus bus;
    for(uint8_t i = 0; i < firefly::EventBus::kCapacity; ++i) {
        expect_true(
            bus.post({firefly::EventType::AlarmTriggered, i, i,
                      firefly::EventPriority::Critical}),
            "fill critical event bus"
        );
    }
    expect_true(
        !bus.post({firefly::EventType::Wake, 0, 200,
                   firefly::EventPriority::Critical}),
        "critical event rejected without refresh slot"
    );
}

static void test_state_store_revision() {
    firefly::StateStore store;
    const uint32_t before = store.revision();
    firefly::BatteryState battery{};
    battery.percent = 73;
    battery.temperature_c = 31;
    battery.battery_mv = 3970;
    battery.valid = true;
    store.setBattery(battery);
    const firefly::SystemState snapshot = store.snapshot();
    expect_true(snapshot.battery.percent == 73, "battery snapshot");
    expect_true(store.revision() == before + 1, "state revision increments");
    store.setBattery(battery);
    expect_true(store.revision() == before + 1, "unchanged state keeps revision");
    store.setSleepState(true, true);
    expect_true(store.snapshot().sleeping && store.snapshot().screen_off,
                "sleep state snapshot");
}

static void test_capability_registry() {
    firefly::CapabilityRegistry capabilities;
    expect_true(!capabilities.has(firefly::Capability::Motion),
                "capability starts unavailable");
    capabilities.set(firefly::Capability::Motion, true);
    expect_true(capabilities.has(firefly::Capability::Motion),
                "capability can be enabled");
    capabilities.set(firefly::Capability::Motion, false);
    expect_true(!capabilities.has(firefly::Capability::Motion),
                "capability can be disabled");
    expect_true(
        firefly::capabilityBit(firefly::Capability::Audio) !=
            firefly::capabilityBit(firefly::Capability::Sd),
        "capability bits are distinct"
    );
}

static void test_app_registry() {
    firefly::AppRegistry registry;
    const uint16_t display = firefly::capabilityBit(firefly::Capability::Display);
    const firefly::AppDescriptor settings{"settings", "Settings", display};
    expect_true(registry.add(settings), "register settings");
    expect_true(registry.count() == 1, "registry count");
    expect_true(registry.find("settings") != nullptr, "find settings");
    expect_true(!registry.add({"settings", "Duplicate", display}),
                "reject duplicate app id");
    expect_true(!registry.add({"", "Empty", display}), "reject empty app id");

    firefly::CapabilityRegistry capabilities;
    expect_true(!registry.available(settings, capabilities),
                "missing capability hides app");
    capabilities.set(firefly::Capability::Display, true);
    expect_true(registry.available(settings, capabilities),
                "available capability shows app");
}

static void test_lifecycle_and_resource_governor() {
    firefly::SystemLifecycle lifecycle;
    expect_true(lifecycle.phase() == firefly::SystemPhase::Booting,
                "lifecycle starts booting");
    expect_true(lifecycle.transition(firefly::SystemPhase::Locked),
                "boot completes to lock");
    expect_true(!lifecycle.transition(firefly::SystemPhase::Updating),
                "locked cannot jump into update");
    expect_true(lifecycle.transition(firefly::SystemPhase::Active),
                "unlock enters active");
    expect_true(!lifecycle.transition(firefly::SystemPhase::Updating),
                "update requires resource governor");

    firefly::ResourceGovernor resources;
    expect_true(resources.acquire(firefly::ResourceKind::AudioPlayback),
                "audio playback lease");
    expect_true(!resources.acquire(firefly::ResourceKind::AudioRecording),
                "recording conflicts with playback");
    expect_true(!resources.acquire(firefly::ResourceKind::Ota),
                "ota conflicts with playback");
    expect_true(!lifecycle.transition(firefly::SystemPhase::Updating, resources),
                "playback blocks update phase");
    resources.release(firefly::ResourceKind::AudioPlayback);
    resources.setConstrained(true);
    expect_true(!resources.canAcquire(firefly::ResourceKind::Ota),
                "power constraint rejects ota");
    expect_true(!lifecycle.transition(firefly::SystemPhase::Updating, resources),
                "power constraint blocks update phase");
    expect_true(!resources.acquire(firefly::ResourceKind::HighRateMotion),
                "power constraint rejects high rate motion");
    resources.setConstrained(false);
    expect_true(resources.canAcquire(firefly::ResourceKind::Ota),
                "idle resources allow ota");
    expect_true(lifecycle.transition(firefly::SystemPhase::Updating, resources),
                "guarded active phase can enter update");
}

static void test_app_manager_publishes_requests() {
    firefly::AppRegistry registry;
    firefly::CapabilityRegistry capabilities;
    firefly::EventBus bus;
    capabilities.set(firefly::Capability::Display, true);
    registry.add({"settings", "Settings",
                  firefly::capabilityBit(firefly::Capability::Display)});
    firefly::AppManager manager(registry, capabilities, bus);
    expect_true(manager.requestOpen("settings", 42), "request known app");
    firefly::SystemEvent event{};
    expect_true(bus.take(event) &&
                event.type == firefly::EventType::AppOpenRequested,
                "app open event published");
    expect_true(!manager.requestOpen("missing", 43), "reject missing app");
    manager.confirmOpened("settings");
    expect_true(manager.hasCreatedPage(), "opened page recorded");
}

class FakePowerDevice : public firefly::PowerDevice {
public:
    firefly::BatteryState readBattery() override {
        firefly::BatteryState state{};
        state.percent = 73;
        state.valid = true;
        return state;
    }

    void setDisplayBrightness(uint8_t value) override {
        brightness = value;
    }

    firefly::PowerButtonEvent readPowerButtonEvent() override {
        const firefly::PowerButtonEvent result = next_button_event;
        next_button_event = firefly::PowerButtonEvent::None;
        return result;
    }

    void shutdown() override {
        shutdown_requested = true;
    }

    uint8_t brightness = 0;
    firefly::PowerButtonEvent next_button_event =
        firefly::PowerButtonEvent::None;
    bool shutdown_requested = false;
};

class FakeClockDevice : public firefly::ClockDevice {
public:
    bool readEpoch(int64_t & epoch_seconds) override {
        if(!read_ok) {
            return false;
        }
        epoch_seconds = epoch;
        return true;
    }

    bool writeEpoch(int64_t epoch_seconds) override {
        if(!write_ok) {
            return false;
        }
        epoch = epoch_seconds;
        last_written = epoch_seconds;
        wrote = true;
        return true;
    }

    bool read_ok = true;
    bool write_ok = true;
    bool wrote = false;
    int64_t epoch = 0;
    int64_t last_written = 0;
};

static void test_hardware_abstraction() {
    FakePowerDevice power;
    const firefly::BatteryState battery = power.readBattery();
    expect_true(battery.valid && battery.percent == 73,
                "power interface returns value state");
    power.setDisplayBrightness(128);
    expect_true(power.brightness == 128, "power interface controls brightness");
    power.next_button_event = firefly::PowerButtonEvent::LongPress;
    expect_true(power.readPowerButtonEvent() ==
                    firefly::PowerButtonEvent::LongPress,
                "power interface exposes semantic button event");
    expect_true(power.readPowerButtonEvent() == firefly::PowerButtonEvent::None,
                "power button event is consumed once");
    power.shutdown();
    expect_true(power.shutdown_requested,
                "power interface exposes controlled shutdown");

    firefly::I2cBusManager i2c(Wire);
    expect_true(i2c.lock(10), "i2c manager acquires mutex");
    i2c.unlock();
    expect_true(&i2c.wire() == &Wire, "i2c manager retains bus");

    firefly::Qmi8658ControlAdapter motion(i2c);
    firefly::Es8311ControlAdapter codec(i2c);
    expect_true(motion.address() == 0x6B, "qmi8658 default address");
    expect_true(codec.address() == 0x18, "es8311 default address");
    expect_true(!motion.readRegisters(0x00, nullptr, 1),
                "register read rejects null output");
    expect_true(!codec.writeRegisters(0x00, nullptr, 1),
                "register write rejects null input");
}

static void test_default_theme_tokens() {
    const firefly::UiTokens tokens = firefly::UiTheme::fireflyDefault();
    expect_true(tokens.bg_base == 0x0041, "AMOLED base is near black");
    expect_true(tokens.radius_card == 24, "card radius token");
    expect_true(tokens.touch_min == 48, "minimum touch target");

    const uint16_t pixels[] = {0x0000, 0x07E0, 0x07E0, 0xFFFF};
    const firefly::UiTokens sampled = firefly::UiTheme::sampleWallpaper(pixels, 2, 2);
    expect_true(sampled.touch_min == 48, "sampled theme keeps geometry");
    expect_true(sampled.firefly_primary != tokens.firefly_primary,
                "wallpaper sampling changes accent");
    expect_true(firefly::UiTheme::samAlert().sam_ignition != tokens.sam_ignition,
                "sam alert has distinct ignition color");
}

static void test_navigation_stack() {
    firefly::NavigationController nav;
    expect_true(nav.current() == firefly::Route::Lock, "starts locked");
    expect_true(nav.open(firefly::Route::Home), "open home");
    expect_true(nav.open(firefly::Route::Settings), "open settings");
    expect_true(nav.back() == firefly::Route::Home, "back to home");
    expect_true(nav.back() == firefly::Route::Lock, "home back locks");

    expect_true(nav.open(firefly::Route::Home), "reopen home");
    for(uint8_t i = 0; i < firefly::NavigationController::kDepth - 2; ++i) {
        expect_true(nav.open(firefly::Route::Tools), "fill navigation stack");
    }
    expect_true(!nav.open(firefly::Route::Diagnostics), "reject stack overflow");
    nav.lock();
    expect_true(nav.current() == firefly::Route::Lock && nav.depth() == 1,
                "lock resets navigation");
}

static void test_overlay_priority_policy() {
    expect_true(firefly::SystemOverlayHost::acceptsPriority(2, 4),
                "alarm can cover charging");
    expect_true(!firefly::SystemOverlayHost::acceptsPriority(4, 2),
                "charging cannot cover alarm");
    expect_true(!firefly::SystemOverlayHost::acceptsPriority(0, 0),
                "priority zero is invalid");
}

static void test_alarm_next_trigger() {
    firefly::AlarmService service;
    firefly::Alarm alarm{};
    alarm.configured = true;
    alarm.enabled = true;
    alarm.hour = 7;
    alarm.minute = 30;
    alarm.days_mask = 0x7F;
    service.set(0, alarm);
    const int64_t now = 1767221940;  // 2026-01-01 07:19:00 +08
    const auto next = service.nextTrigger(now);
    expect_true(next.valid, "next alarm exists");
    expect_true(next.slot == 0, "next alarm slot");
    expect_true(next.epoch_seconds > now, "next alarm is future");
}

static void test_time_service_invalid_rtc() {
    FakeClockDevice clock;
    clock.read_ok = false;
    firefly::TimeService time(clock);
    expect_true(!time.begin(), "invalid rtc begin fails");
    const firefly::TimeSnapshot snapshot = time.now();
    expect_true(!snapshot.valid, "invalid rtc snapshot marked invalid");
    expect_true(snapshot.epoch_seconds == 0, "invalid rtc does not fake date");
    time.tick();
    expect_true(!time.now().valid, "invalid rtc tick stays invalid");
}

static void test_time_service_reload_set_and_tick() {
    FakeClockDevice clock;
    clock.epoch = 1000;
    firefly::TimeService time(clock);
    expect_true(time.begin(), "valid rtc begin succeeds");
    expect_true(time.now().valid && time.now().epoch_seconds == 1000,
                "time service reads rtc");
    time.tick();
    expect_true(time.now().epoch_seconds == 1001, "tick advances one second");
    expect_true(time.setLocalTime(2000), "set local time succeeds");
    expect_true(clock.wrote && clock.last_written == 2000,
                "set local time writes rtc");
    time.tick();
    expect_true(time.now().epoch_seconds == 2001,
                "tick advances manually set time");
    clock.epoch = 3000;
    const firefly::TimeSnapshot reloaded = time.reloadRtc();
    expect_true(reloaded.valid && reloaded.epoch_seconds == 3000,
                "reload rtc refreshes snapshot");
}

static void test_countdown_timer_uses_target_time() {
    firefly::CountdownTimer timer;
    timer.start(10 * 60 * 1000UL, 1000);
    expect_true(timer.running(), "countdown timer starts running");
    expect_true(timer.remainingMs(1000) == 600000UL,
                "countdown stores full duration");
    expect_true(timer.remainingMs(601000) == 0,
                "countdown reaches zero at target");
    expect_true(timer.expired(601001), "countdown reports expired after target");
}

static void test_countdown_pause_resume_and_one_shot_expiry() {
    firefly::CountdownTimer timer;
    timer.start(60000, 1000);
    timer.pause(11000);
    expect_true(!timer.running() && timer.remainingMs(50000) == 50000,
                "paused countdown preserves remaining time");
    timer.resume(20000);
    expect_true(timer.remainingMs(69999) == 1,
                "resumed countdown uses a new target");
    expect_true(timer.consumeExpired(70000),
                "countdown publishes expiry once");
    expect_true(!timer.consumeExpired(70001),
                "countdown expiry is one shot");
}

static void test_stopwatch_survives_page_visibility_changes() {
    firefly::StopwatchSession stopwatch;
    stopwatch.start(1000000);
    expect_true(stopwatch.elapsedUs(4000000) == 3000000,
                "stopwatch follows monotonic time without page state");
    stopwatch.pause(5000000);
    stopwatch.start(9000000);
    expect_true(stopwatch.elapsedUs(10000000) == 5000000,
                "stopwatch resumes accumulated time");
}

static void test_stopwatch_uses_monotonic_time() {
    firefly::StopwatchSession stopwatch;
    stopwatch.start(1000000LL);
    expect_true(stopwatch.running(), "stopwatch starts running");
    expect_true(stopwatch.elapsedUs(2500000LL) == 1500000LL,
                "stopwatch elapsed follows monotonic time");
    stopwatch.pause(3000000LL);
    expect_true(!stopwatch.running(), "stopwatch pauses");
    expect_true(stopwatch.elapsedUs(5000000LL) == 2000000LL,
                "paused stopwatch holds elapsed");
    stopwatch.start(7000000LL);
    expect_true(stopwatch.elapsedUs(8000000LL) == 3000000LL,
                "stopwatch resumes from accumulated elapsed");
    stopwatch.reset();
    expect_true(stopwatch.elapsedUs(9000000LL) == 0,
                "stopwatch reset clears elapsed");
}

static void test_settings_app_command_queue() {
    firefly::SettingsCommandQueue queue;
    expect_true(queue.post({firefly::SettingsCommandType::SetBrightness, 72}),
                "settings command queues brightness");
    firefly::SettingsCommand command{};
    expect_true(queue.take(command) &&
                command.type == firefly::SettingsCommandType::SetBrightness &&
                command.value == 72,
                "settings command preserves payload");
    expect_true(!queue.take(command), "settings command queue drains");
}

static void test_settings_commands_preserve_time_and_alarm_payloads() {
    firefly::SettingsCommandQueue queue;
    firefly::SettingsCommand time{};
    time.type = firefly::SettingsCommandType::SetLocalTime;
    time.value = 1783008000LL;
    expect_true(queue.post(time), "settings accepts epoch command");
    firefly::SettingsCommand actual{};
    expect_true(queue.take(actual) && actual.value == 1783008000LL,
                "settings preserves 64 bit epoch");

    firefly::SettingsCommand alarm{};
    alarm.type = firefly::SettingsCommandType::SaveAlarm;
    alarm.slot = 1;
    alarm.alarm.configured = true;
    alarm.alarm.hour = 7;
    alarm.alarm.minute = 30;
    expect_true(queue.post(alarm), "settings accepts alarm command");
    expect_true(queue.take(actual) && actual.slot == 1 &&
                    actual.alarm.minute == 30,
                "settings preserves fixed alarm payload");
}

static void test_alarm_service_publishes_trigger_event_once() {
    firefly::AlarmService service;
    firefly::EventBus events;
    const int64_t now = 1767222600;
    const time_t raw = static_cast<time_t>(now);
    struct tm local{};
    localtime_r(&raw, &local);
    firefly::Alarm alarm{};
    alarm.configured = true;
    alarm.enabled = true;
    alarm.hour = static_cast<uint8_t>(local.tm_hour);
    alarm.minute = static_cast<uint8_t>(local.tm_min);
    alarm.days_mask = 0x7F;
    service.set(1, alarm);

    expect_true(service.publishTrigger(now, 1234, events),
                "alarm publishes due trigger");
    firefly::SystemEvent event{};
    expect_true(events.take(event) &&
                    event.type == firefly::EventType::AlarmTriggered &&
                    event.value == 1 && event.timestamp_ms == 1234,
                "alarm trigger event carries slot");
    expect_true(!service.publishTrigger(now, 1235, events),
                "alarm trigger publishes once per minute");
}

static void test_sd_paths_stay_inside_managed_root() {
    expect_true(firefly::SdCardDevice::isSafeRelativePath("Music/track.wav"),
                "SD accepts managed relative file");
    expect_true(firefly::SdCardDevice::isSafeRelativePath("Themes/Firefly/theme.json"),
                "SD accepts nested managed relative file");
    expect_true(!firefly::SdCardDevice::isSafeRelativePath("/Music/track.wav"),
                "SD rejects absolute path");
    expect_true(!firefly::SdCardDevice::isSafeRelativePath("Music/../Backups/file"),
                "SD rejects parent traversal");
    expect_true(!firefly::SdCardDevice::isSafeRelativePath("Music\\track.wav"),
                "SD rejects backslash traversal");
    expect_true(firefly::StorageService::isManagedPath(
                    "/FireflyOS/Themes/Firefly/theme.json"),
                "storage accepts a managed absolute path");
    expect_true(!firefly::StorageService::isManagedPath(
                    "/FireflyOS/Other/file.bin"),
                "storage rejects an unregistered top-level directory");
    expect_true(!firefly::StorageService::isManagedPath(
                    "/FireflyOS/Music/../Backups/file.bin"),
                "storage rejects parent traversal");
}

static void test_sd_removal_requires_two_consecutive_failures() {
    firefly::SdFailureMonitor monitor;
    expect_true(!monitor.noteResult(false), "first SD failure is tolerated");
    expect_true(monitor.noteResult(true) == false,
                "successful SD access clears failure count");
    expect_true(!monitor.noteResult(false), "failure count restarts after success");
    expect_true(monitor.noteResult(false),
                "second consecutive SD failure reports removal");
}

static void test_theme_manifest_validation() {
    static const char valid[] =
        "{\"schema\":1,\"id\":\"firefly-night\",\"name\":\"Firefly Night\","
        "\"author\":\"FireflyOS\",\"palette\":{"
        "\"bg_base\":\"#05090C\",\"bg_surface\":\"#0C1820\","
        "\"primary\":\"#5FE7C7\",\"secondary\":\"#6EC4D6\","
        "\"critical\":\"#FF5A5F\"},"
        "\"wallpaper\":\"wallpaper.rgb565\","
        "\"wallpaper_width\":410,\"wallpaper_height\":502,"
        "\"glance\":\"glance.png\","
        "\"icon_pack\":\"icons\"}";
    firefly::ThemePackageService service;
    firefly::ThemeManifest manifest{};
    firefly::ThemeValidationError error = firefly::ThemeValidationError::None;
    expect_true(service.parseManifest(valid, sizeof(valid) - 1, manifest, error),
                "valid theme manifest parses");
    expect_true(strcmp(manifest.id, "firefly-night") == 0,
                "theme manifest preserves id");
    expect_true(manifest.palette[2] == 0x5FE7C7,
                "theme manifest parses primary color");

    static const char traversal[] =
        "{\"schema\":1,\"id\":\"bad-theme\",\"name\":\"Bad\","
        "\"author\":\"Test\",\"palette\":{"
        "\"bg_base\":\"#05090C\",\"bg_surface\":\"#0C1820\","
        "\"primary\":\"#5FE7C7\",\"secondary\":\"#6EC4D6\","
        "\"critical\":\"#FF5A5F\"},"
        "\"wallpaper\":\"../wallpaper.rgb565\","
        "\"wallpaper_width\":410,\"wallpaper_height\":502,"
        "\"glance\":\"glance.png\","
        "\"icon_pack\":\"icons\"}";
    expect_true(!service.parseManifest(traversal, sizeof(traversal) - 1,
                                       manifest, error),
                "theme manifest rejects parent traversal");
}

static void test_audio_session_priority() {
    firefly::AudioSessionArbiter arbiter;
    expect_true(arbiter.acquire(firefly::AudioUse::Music),
                "music acquires audio");
    expect_true(arbiter.acquire(firefly::AudioUse::System),
                "system sound preempts music");
    expect_true(arbiter.acquire(firefly::AudioUse::Alarm),
                "alarm preempts system sound");
    expect_true(arbiter.current() == firefly::AudioUse::Alarm,
                "alarm owns audio");
    expect_true(!arbiter.acquire(firefly::AudioUse::Recorder),
                "recorder cannot interrupt alarm");
    arbiter.release(firefly::AudioUse::Music);
    expect_true(arbiter.current() == firefly::AudioUse::Alarm,
                "non-owner cannot release audio");
    arbiter.release(firefly::AudioUse::Alarm);
    expect_true(arbiter.current() == firefly::AudioUse::None,
                "owner releases audio");
}

static void test_pcm_wav_header_round_trip() {
    uint8_t header[44]{};
    expect_true(firefly::AudioService::buildWavHeader(
                    header, sizeof(header), 32000, 16000, 1),
                "build mono WAV header");
    firefly::WavInfo info{};
    expect_true(firefly::AudioService::parseWavHeader(
                    header, sizeof(header), info),
                "parse generated WAV header");
    expect_true(info.sample_rate == 16000 && info.channels == 1 &&
                    info.bits_per_sample == 16 && info.data_bytes == 32000,
                "WAV metadata round trips");
    header[20] = 3;
    expect_true(!firefly::AudioService::parseWavHeader(
                    header, sizeof(header), info),
                "reject non-PCM WAV");
}

static void test_alarm_ringtone_resources() {
    const char * expected[] = {
        "Trailblaze", "Starglow", "Night Sky", "Classic Bell"
    };
    for(uint8_t i = 0; i < firefly::AlarmService::kRingtoneCount; ++i) {
        const firefly::AlarmToneResource & resource =
            firefly::AlarmService::ringtoneResource(i);
        expect_true(strcmp(resource.name, expected[i]) == 0,
                    "ringtone index remains stable");
        expect_true(resource.samples != nullptr && resource.frames > 0,
                    "ringtone has built-in PCM");
        expect_true(resource.sample_rate == 16000 && resource.loop,
                    "ringtone is looping 16 kHz mono PCM");
        expect_true(resource.frames <=
                        firefly::AlarmService::kMaximumRingtoneFrames,
                    "ringtone stays within twenty-second budget");
    }
}

static void test_media_app_fixed_boundaries() {
    firefly::FileListItem music{};
    strlcpy(music.name, "track.wav", sizeof(music.name));
    expect_true(firefly::FilesApp::canDeleteFile("Music", music),
                "ordinary music file can be deleted");
    expect_true(!firefly::FilesApp::canDeleteFile("Logs", music),
                "log files cannot be deleted from FilesApp");
    strlcpy(music.name, "../escape.wav", sizeof(music.name));
    expect_true(!firefly::FilesApp::canDeleteFile("Music", music),
                "delete rejects path escape");
    expect_true(firefly::FilesApp::kPageSize == 32 &&
                    firefly::MusicApp::kMaxTracks == 128,
                "media indexes use fixed capacities");

    char name[48]{};
    expect_true(firefly::RecorderApp::makeRecordingName(
                    0, 42, name, sizeof(name)),
                "fallback recording name formats");
    expect_true(strcmp(name, "REC_000042.wav") == 0,
                "fallback recording name is monotonic");
}

static void test_file_scan_page_is_fixed_and_bounded() {
    firefly::FileScanPage page{};
    expect_true(firefly::FileScanService::kPageSize == 32,
                "file scanner page holds 32 items");
    expect_true(firefly::FileScanService::kEntriesPerTick == 4,
                "file scanner work per tick is bounded");
    expect_true(sizeof(page.items[0].name) == 48,
                "file scanner names stay fixed");
}

static void test_storage_settings_defaults_and_namespaces() {
    firefly::SystemSettings settings{};
    expect_true(settings.schema_version == 1,
                "settings schema version");
    expect_true(settings.volume == 50,
                "default volume");
    expect_true(settings.brightness == 128,
                "default brightness");
    expect_true(settings.auto_sleep_seconds == 30,
                "default sleep timeout");
    expect_true(settings.hide_notification_content,
                "notification content hidden by default");
    expect_true(settings.wrist_raise_enabled,
                "wrist raise enabled by default");
    expect_true(strcmp(settings.theme_id, "system-default") == 0,
                "default theme id");
    expect_true(strcmp(firefly::StorageService::kSystemNamespace,
                       "ff_sys") == 0 &&
                    strcmp(firefly::StorageService::kAlarmNamespace,
                           "ff_alarm") == 0 &&
                    strcmp(firefly::StorageService::kPairNamespace,
                           "ff_pair") == 0 &&
                    strcmp(firefly::StorageService::kStatsNamespace,
                           "ff_stats") == 0,
                "storage namespaces are fixed");
}

static void test_storage_legacy_migration_preserves_alarm_fields() {
    firefly::LegacyStorageSnapshot legacy{};
    legacy.has_volume = true;
    legacy.volume = 73;
    for(uint8_t slot = 0; slot < firefly::AlarmService::kSlots; ++slot) {
        legacy.has_alarm[slot] = true;
        legacy.alarms[slot].configured = true;
        legacy.alarms[slot].enabled = slot == 0;
        legacy.alarms[slot].hour = static_cast<uint8_t>(6 + slot);
        legacy.alarms[slot].minute = static_cast<uint8_t>(15 + slot);
        legacy.alarms[slot].days_mask = static_cast<uint8_t>(0x3E + slot);
        legacy.alarms[slot].ringtone = static_cast<uint8_t>(slot + 1);
        snprintf(legacy.alarms[slot].name,
                 sizeof(legacy.alarms[slot].name),
                 "Legacy %u", static_cast<unsigned>(slot));
    }

    firefly::SystemSettings settings{};
    firefly::Alarm migrated[firefly::AlarmService::kSlots]{};
    bool present[firefly::AlarmService::kSlots]{};
    firefly::StorageService::applyLegacySnapshot(
        legacy, settings, migrated, present);

    expect_true(settings.volume == 73,
                "legacy volume migrates");
    for(uint8_t slot = 0; slot < firefly::AlarmService::kSlots; ++slot) {
        expect_true(present[slot], "legacy alarm remains present");
        expect_true(migrated[slot].configured == legacy.alarms[slot].configured &&
                        migrated[slot].enabled == legacy.alarms[slot].enabled &&
                        migrated[slot].hour == legacy.alarms[slot].hour &&
                        migrated[slot].minute == legacy.alarms[slot].minute &&
                        migrated[slot].days_mask == legacy.alarms[slot].days_mask &&
                        migrated[slot].ringtone == legacy.alarms[slot].ringtone &&
                        strcmp(migrated[slot].name,
                               legacy.alarms[slot].name) == 0,
                    "legacy alarm fields migrate exactly");
    }
}

static void test_calendar_month_boundaries() {
    const firefly::CalendarMonth feb_2028 =
        firefly::CalendarModel::buildMonth(2028, 2, 15);
    expect_true(feb_2028.year == 2028 && feb_2028.month == 2,
                "calendar keeps requested month");
    expect_true(feb_2028.days_in_month == 29, "calendar handles leap february");
    expect_true(feb_2028.first_weekday == 2, "calendar maps Tuesday start");
    expect_true(feb_2028.cells[2].day == 1 && feb_2028.cells[2].in_current_month,
                "calendar places first day after sunday slots");

    const firefly::CalendarMonth sunday_start =
        firefly::CalendarModel::buildMonth(2026, 2, 1);
    expect_true(sunday_start.first_weekday == 0,
                "calendar uses sunday as first column");
    expect_true(sunday_start.cells[0].day == 1 && sunday_start.cells[0].today,
                "calendar marks today in sunday slot");

    const firefly::CalendarMonth next_year =
        firefly::CalendarModel::shiftMonth(2027, 12, 1);
    expect_true(next_year.year == 2028 && next_year.month == 1,
                "calendar rolls december to january");
    const firefly::CalendarMonth previous_year =
        firefly::CalendarModel::shiftMonth(2028, 1, -1);
    expect_true(previous_year.year == 2027 && previous_year.month == 12,
                "calendar rolls january to december");
}

static void test_calendar_agenda_truncates_to_eight() {
    firefly::CalendarAgendaCache agenda;
    firefly::CalendarSummary summaries[10]{};
    for(uint8_t i = 0; i < 10; ++i) {
        summaries[i].valid = true;
        summaries[i].start_epoch = 1800000000LL + i;
        snprintf(summaries[i].title, sizeof(summaries[i].title),
                 "Event %u", static_cast<unsigned>(i + 1U));
    }

    agenda.setSummaries(summaries, 10, 1800001234LL);
    expect_true(agenda.count() == firefly::CalendarAgendaCache::kMaxSummaries,
                "calendar agenda caps summaries");
    expect_true(strcmp(agenda.at(7).title, "Event 8") == 0,
                "calendar agenda preserves first eight summaries");
    expect_true(agenda.lastUpdatedEpoch() == 1800001234LL,
                "calendar agenda stores sync timestamp");
}

static void test_calculator_engine_basic_operations() {
    firefly::CalculatorEngine calculator;
    expect_true(calculator.setExpression("12.5+3.5"),
                "calculator accepts decimal expression");
    expect_true(calculator.evaluate(), "calculator evaluates addition");
    expect_true(strcmp(calculator.display(), "16") == 0,
                "calculator trims trailing zeroes");

    calculator.clear();
    expect_true(calculator.setExpression("-6*2"),
                "calculator accepts negative first operand");
    expect_true(calculator.evaluate(), "calculator evaluates multiplication");
    expect_true(strcmp(calculator.display(), "-12") == 0,
                "calculator shows negative result");

    calculator.clear();
    expect_true(calculator.setExpression("7/2"), "calculator accepts division");
    expect_true(calculator.evaluate(), "calculator evaluates division");
    expect_true(strcmp(calculator.display(), "3.5") == 0,
                "calculator keeps decimal result");
}

static void test_calculator_engine_limits_and_errors() {
    firefly::CalculatorEngine calculator;
    expect_true(!calculator.setExpression("1234567890123456789012345"),
                "calculator rejects expressions over 24 chars");
    expect_true(calculator.setExpression("4/0"), "calculator accepts divide expression");
    expect_true(!calculator.evaluate(), "calculator rejects divide by zero");
    expect_true(strcmp(calculator.display(), "Divide by zero") == 0,
                "calculator reports divide by zero");
    calculator.clear();
    expect_true(strcmp(calculator.display(), "0") == 0,
                "calculator clear resets display");

    expect_true(calculator.setExpression("1000000000*9"),
                "calculator accepts large visible result");
    expect_true(calculator.evaluate(), "calculator evaluates large result");
    expect_true(strlen(calculator.display()) <= firefly::CalculatorEngine::kMaxDisplayChars,
                "calculator display fits visible limit");
}

static void test_calculator_key_input_flow() {
    firefly::CalculatorEngine calculator;
    expect_true(calculator.inputKey("1"), "calculator accepts first key");
    expect_true(calculator.inputKey("2"), "calculator appends digit key");
    expect_true(calculator.inputKey("."), "calculator accepts decimal key");
    expect_true(calculator.inputKey("5"), "calculator appends decimal digit");
    expect_true(calculator.inputKey("+"), "calculator accepts operator key");
    expect_true(calculator.inputKey("3"), "calculator accepts rhs key");
    expect_true(strcmp(calculator.display(), "12.5+3") == 0,
                "calculator displays expression while editing");
    expect_true(calculator.inputKey("="), "calculator evaluates equals key");
    expect_true(strcmp(calculator.display(), "15.5") == 0,
                "calculator displays key-entered result");
    expect_true(calculator.inputKey("C"), "calculator clear key succeeds");
    expect_true(strcmp(calculator.display(), "0") == 0,
                "calculator clear key resets display");
}

static void test_tools_command_queue_is_fixed_fifo() {
    firefly::ToolsCommandQueue queue;
    for(uint8_t i = 0; i < firefly::ToolsCommandQueue::kCapacity; ++i) {
        expect_true(queue.post({firefly::ToolsCommandType::SetBrightness,
                                static_cast<uint8_t>(20U + i)}),
                    "tools queue accepts command within capacity");
    }
    expect_true(!queue.post({firefly::ToolsCommandType::SetBrightness, 255}),
                "tools queue rejects overflow");

    firefly::ToolsCommand command{};
    expect_true(queue.take(command), "tools queue returns first command");
    expect_true(command.type == firefly::ToolsCommandType::SetBrightness &&
                    command.value == 20,
                "tools queue preserves FIFO order");
    for(uint8_t i = 1; i < firefly::ToolsCommandQueue::kCapacity; ++i) {
        expect_true(queue.take(command), "tools queue drains queued command");
    }
    expect_true(!queue.take(command), "tools queue reports empty state");
}

static void test_flashlight_session_policy() {
    firefly::FlashlightSession flashlight;
    const firefly::FlashlightPowerState ok{50, 30, true};
    expect_true(flashlight.start(ok, 1000, 72), "flashlight starts when safe");
    expect_true(flashlight.active(60999), "flashlight remains active before limit");
    expect_true(!flashlight.active(61001), "flashlight stops after sixty seconds");
    expect_true(flashlight.originalBrightness() == 72,
                "flashlight remembers original brightness");

    flashlight.stop();
    const firefly::FlashlightPowerState low{14, 30, true};
    expect_true(!flashlight.start(low, 2000, 90),
                "flashlight rejects low battery");
    const firefly::FlashlightPowerState hot{80, 50, true};
    expect_true(!flashlight.start(hot, 2000, 90),
                "flashlight rejects unsafe temperature");

    expect_true(flashlight.start(ok, 3000, 128),
                "flashlight restarts after safe state");
    flashlight.closeFromUser();
    expect_true(!flashlight.active(3001), "flashlight closes from input");
}

static void test_flashlight_controller_posts_brightness_commands() {
    firefly::FlashlightController controller;
    const firefly::FlashlightPowerState safe{80, 28, true};
    expect_true(controller.start(safe, 1000, 96),
                "flashlight controller starts safe session");

    firefly::ToolsCommand command{};
    expect_true(controller.takeCommand(command),
                "flashlight start posts brightness command");
    expect_true(command.type == firefly::ToolsCommandType::SetBrightness &&
                    command.value == 255,
                "flashlight start requests maximum brightness");
    expect_true(!controller.tick(60999),
                "flashlight controller stays active before timeout");
    expect_true(controller.tick(61000),
                "flashlight controller closes at timeout");
    expect_true(controller.takeCommand(command),
                "flashlight timeout posts restore command");
    expect_true(command.value == 96,
                "flashlight restores original brightness");
    expect_true(!controller.stop(),
                "flashlight controller does not restore twice");
}

static void test_power_state_machine_timing() {
    firefly::PowerService power;
    power.configure({30000, 5000, 3000});
    power.onActivity(1000);
    expect_true(power.evaluateIdle(30999) == firefly::PowerMode::Active,
                "power remains active before idle timeout");
    expect_true(power.evaluateIdle(31001) == firefly::PowerMode::IdleDim,
                "power dims after idle timeout");
    expect_true(power.evaluateIdle(36001) == firefly::PowerMode::Glance,
                "power enters glance after dim interval");
    expect_true(power.evaluateIdle(39001) == firefly::PowerMode::ScreenOff,
                "power turns screen off after glance interval");

    power.onActivity(40000);
    expect_true(power.evaluateIdle(40001) == firefly::PowerMode::Active,
                "activity resets power timing");
}

static void test_power_battery_priority_and_thresholds() {
    firefly::PowerService power;
    power.configure({30000, 5000, 3000});
    power.onActivity(1000);

    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    battery.temperature_c = 50;
    battery.charging = true;
    power.setBatteryState(battery);
    expect_true(power.evaluate(1001) == firefly::PowerMode::ThermalProtection,
                "thermal protection overrides charging");

    battery.temperature_c = 30;
    battery.percent = 4;
    power.setBatteryState(battery);
    expect_true(power.evaluate(1001) == firefly::PowerMode::Charging,
                "safe charging overrides low battery modes");

    battery.charging = false;
    battery.vbus_present = false;
    battery.percent = 25;
    power.setBatteryState(battery);
    expect_true(power.evaluate(1001) == firefly::PowerMode::Saver,
                "power saver begins at twenty five percent");
    battery.percent = 15;
    power.setBatteryState(battery);
    expect_true(power.evaluate(1001) == firefly::PowerMode::LowBattery,
                "low battery begins at fifteen percent");
    battery.percent = 5;
    power.setBatteryState(battery);
    expect_true(power.evaluate(1001) == firefly::PowerMode::CriticalBattery,
                "critical battery begins at five percent");
}

static void test_time_service_network_sync_is_deferred_for_alarm() {
    FakeClockDevice clock;
    clock.epoch = 1000;
    firefly::TimeService time(clock);
    expect_true(time.begin(), "network sync test starts from valid rtc");
    expect_true(time.applyNetworkTime(1001, false) && !clock.wrote,
                "network difference within two seconds does not rewrite rtc");
    expect_true(time.applyNetworkTime(1100, true) && time.networkSyncPending() &&
                    !clock.wrote,
                "alarm defers a material network time correction");
    expect_true(!time.flushDeferredNetworkTime(true) && !clock.wrote,
                "deferred network time remains pending while alarm rings");
    expect_true(time.flushDeferredNetworkTime(false) && clock.wrote &&
                    clock.last_written == 1100 && !time.networkSyncPending(),
                "deferred network time writes rtc after alarm closes");
}

class FakeWifiRadio : public firefly::WifiRadio {
public:
    bool connectStation(const char * ssid, const char * password) override {
        ++connect_calls;
        strlcpy(last_ssid, ssid ? ssid : "", sizeof(last_ssid));
        strlcpy(last_password, password ? password : "", sizeof(last_password));
        return connect_result;
    }

    void useMinimumModemPowerSave() override {
        ++power_save_calls;
    }

    void disconnectAndPowerOff() override {
        ++power_off_calls;
    }

    firefly::WifiLinkState linkState() const override {
        return link_state;
    }

    bool connect_result = true;
    firefly::WifiLinkState link_state = firefly::WifiLinkState::Connecting;
    uint8_t connect_calls = 0;
    uint8_t power_save_calls = 0;
    uint8_t power_off_calls = 0;
    char last_ssid[33]{};
    char last_password[65]{};
};

class FakeWifiCredentialStore : public firefly::WifiCredentialStore {
public:
    bool loadWifiCredentials(firefly::WifiCredentials & output) override {
        output = saved;
        return load_result;
    }

    bool saveWifiCredentials(
        const firefly::WifiCredentials & credentials) override {
        ++save_calls;
        if(!save_result) return false;
        saved = credentials;
        return true;
    }

    bool clearWifiCredentials() override {
        ++clear_calls;
        saved = {};
        return clear_result;
    }

    firefly::WifiCredentials saved{};
    bool load_result = true;
    bool save_result = true;
    bool clear_result = true;
    uint8_t save_calls = 0;
    uint8_t clear_calls = 0;
};

static uint16_t build_wifi_provision_payload(
    uint8_t * output,
    size_t capacity,
    int64_t expires_epoch,
    const uint8_t nonce[8],
    const char * ssid,
    const char * password) {
    const size_t ssid_length = strlen(ssid);
    const size_t password_length = strlen(password);
    const size_t required = 19 + ssid_length + password_length;
    if(capacity < required) return 0;
    size_t offset = 0;
    output[offset++] = 1;
    for(uint8_t index = 0; index < 8; ++index) {
        output[offset++] = static_cast<uint8_t>(
            (static_cast<uint64_t>(expires_epoch) >> (index * 8)) & 0xFF
        );
    }
    memcpy(output + offset, nonce, 8);
    offset += 8;
    output[offset++] = static_cast<uint8_t>(ssid_length);
    memcpy(output + offset, ssid, ssid_length);
    offset += ssid_length;
    output[offset++] = static_cast<uint8_t>(password_length);
    memcpy(output + offset, password, password_length);
    offset += password_length;
    return static_cast<uint16_t>(offset);
}

static uint16_t build_wifi_provision_payload_v2(
    uint8_t * output,
    size_t capacity,
    uint8_t ttl_seconds,
    const uint8_t nonce[8],
    const char * ssid,
    const char * password) {
    const size_t ssid_length = strlen(ssid);
    const size_t password_length = strlen(password);
    const size_t required = 12 + ssid_length + password_length;
    if(capacity < required) return 0;
    size_t offset = 0;
    output[offset++] = 2;
    output[offset++] = ttl_seconds;
    memcpy(output + offset, nonce, 8);
    offset += 8;
    output[offset++] = static_cast<uint8_t>(ssid_length);
    memcpy(output + offset, ssid, ssid_length);
    offset += ssid_length;
    output[offset++] = static_cast<uint8_t>(password_length);
    memcpy(output + offset, password, password_length);
    offset += password_length;
    return static_cast<uint16_t>(offset);
}

static void test_wifi_provisioning_v2_uses_monotonic_ttl_without_rtc() {
    FakeWifiRadio radio;
    FakeWifiCredentialStore store;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power, store);
    const uint8_t nonce[8] = {3, 1, 4, 1, 5, 9, 2, 6};
    uint8_t payload[128]{};
    expect_true(!wifi.stageProvisioning(payload, 0, 999, 0),
                "zero-length provisioning payload is rejected safely");
    uint16_t length = build_wifi_provision_payload_v2(
        payload, sizeof(payload), 60, nonce, "First-Boot", "secret");
    expect_true(wifi.stageProvisioning(payload, length, 1000, 0),
                "v2 provisioning accepts a monotonic TTL without RTC");
    expect_true(wifi.confirmProvisioning(false, 1001),
                "v2 first-boot provisioning can leave the confirmation gate");
    expect_true(!wifi.stageProvisioning(payload, length, 1002, 0),
                "v2 nonce cannot be replayed during its monotonic window");

    payload[1] = 0;
    expect_true(!wifi.stageProvisioning(payload, length, 1003, 0),
                "v2 rejects a zero TTL");
    payload[1] = 61;
    expect_true(!wifi.stageProvisioning(payload, length, 1004, 0),
                "v2 rejects TTL beyond sixty seconds");

    expect_true(wifi.clearSensitiveState(),
                "factory-reset hook clears Wi-Fi sensitive state");
    payload[1] = 60;
    expect_true(wifi.stageProvisioning(payload, length, 1005, 0),
                "sensitive-state cleanup clears the nonce replay cache");
    expect_true(wifi.confirmProvisioning(false, 1006),
                "post-cleanup request can leave the confirmation gate");

    expect_true(wifi.clearSensitiveState(),
                "nonce capacity test starts from an empty cache");
    for(uint8_t index = 0; index < firefly::WifiService::kRememberedNonces;
        ++index) {
        uint8_t unique_nonce[8] = {index, 2, 3, 4, 5, 6, 7, 8};
        length = build_wifi_provision_payload_v2(
            payload, sizeof(payload), 60, unique_nonce,
            "Capacity", "secret");
        expect_true(wifi.stageProvisioning(payload, length, 2000, 0) &&
                        wifi.confirmProvisioning(false, 2000),
                    "each live nonce occupies one fixed cache slot");
    }
    uint8_t overflow_nonce[8] = {99, 2, 3, 4, 5, 6, 7, 8};
    length = build_wifi_provision_payload_v2(
        payload, sizeof(payload), 60, overflow_nonce, "Capacity", "secret");
    expect_true(!wifi.stageProvisioning(payload, length, 2001, 0),
                "a ninth live nonce is rejected instead of evicting replay history");
    expect_true(wifi.stageProvisioning(payload, length, 62001, 0),
                "an expired nonce slot can be reused safely");
    expect_true(wifi.confirmProvisioning(false, 62001),
                "reused nonce slot leaves the confirmation gate");

    expect_true(wifi.clearSensitiveState(),
                "short TTL confirmation test starts clean");
    uint8_t short_nonce[8] = {7, 7, 7, 7, 7, 7, 7, 7};
    length = build_wifi_provision_payload_v2(
        payload, sizeof(payload), 1, short_nonce, "Short", "secret");
    expect_true(wifi.stageProvisioning(payload, length, 70000, 0),
                "short TTL request enters the confirmation gate");
    wifi.tick(71001);
    expect_true(wifi.provisioningSnapshot().status ==
                    firefly::WifiProvisioningStatus::Timeout &&
                    !wifi.confirmProvisioning(false, 71001),
                "unanswered confirmation expires without a user callback");
    uint8_t next_nonce[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    length = build_wifi_provision_payload_v2(
        payload, sizeof(payload), 60, next_nonce, "After timeout", "secret");
    expect_true(wifi.stageProvisioning(payload, length, 71002, 0),
                "a later request is accepted after proactive expiry");
    wifi.confirmProvisioning(false, 71002);
}

static void test_wifi_provisioning_is_confirmed_and_persisted_after_connect() {
    FakeWifiRadio radio;
    FakeWifiCredentialStore store;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power, store);

    const uint8_t nonce[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t payload[128]{};
    const uint16_t length = build_wifi_provision_payload(
        payload, sizeof(payload), 1060, nonce, "Firefly-Lab", "secret"
    );
    expect_true(wifi.stageProvisioning(payload, length, 1000, 1000),
                "authenticated Wi-Fi payload is staged");
    const firefly::WifiProvisioningSnapshot pending =
        wifi.provisioningSnapshot();
    expect_true(pending.status == firefly::WifiProvisioningStatus::AwaitingUser &&
                    strcmp(pending.ssid, "Firefly-Lab") == 0 &&
                    store.save_calls == 0,
                "staged Wi-Fi request hides password and is not persisted");

    expect_true(wifi.confirmProvisioning(true, 1010),
                "watch confirmation starts Wi-Fi connection");
    expect_true(wifi.provisioningSnapshot().status ==
                    firefly::WifiProvisioningStatus::Connecting &&
                    store.save_calls == 0,
                "credentials remain volatile while connecting");
    radio.link_state = firefly::WifiLinkState::Connected;
    wifi.tick(1020);
    expect_true(store.save_calls == 1 && store.saved.valid &&
                    strcmp(store.saved.ssid, "Firefly-Lab") == 0 &&
                    strcmp(store.saved.password, "secret") == 0 &&
                    wifi.provisioningSnapshot().status ==
                        firefly::WifiProvisioningStatus::Success &&
                    wifi.mode() == firefly::WifiMode::Off,
                "successful connection atomically persists then powers off");

    const uint8_t forget_payload[2] = {1, 0};
    expect_true(wifi.stageProvisioning(forget_payload, sizeof(forget_payload),
                                       2000, 1000) &&
                    wifi.provisioningSnapshot().status ==
                        firefly::WifiProvisioningStatus::AwaitingForget,
                "forget network requires an on-watch confirmation");
    expect_true(wifi.confirmProvisioning(true, 2010) &&
                    wifi.provisioningSnapshot().status ==
                        firefly::WifiProvisioningStatus::Forgotten &&
                    store.clear_calls == 1 && !wifi.provisioned(),
                "confirmed forget clears durable and in-memory credentials");
}

static void test_wifi_provisioning_rejects_expired_duplicate_and_unconfirmed() {
    FakeWifiRadio radio;
    FakeWifiCredentialStore store;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power, store);
    const uint8_t nonce[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    uint8_t payload[128]{};

    uint16_t length = build_wifi_provision_payload(
        payload, sizeof(payload), 999, nonce, "Expired", "secret"
    );
    expect_true(!wifi.stageProvisioning(payload, length, 0, 1000),
                "expired provisioning nonce is rejected");
    length = build_wifi_provision_payload(
        payload, sizeof(payload), 1061, nonce, "TooFar", "secret"
    );
    expect_true(!wifi.stageProvisioning(payload, length, 0, 1000),
                "nonce beyond sixty seconds is rejected");
    length = build_wifi_provision_payload(
        payload, sizeof(payload), 1060, nonce, "Firefly-Lab", "secret"
    );
    expect_true(wifi.stageProvisioning(payload, length, 0, 1000),
                "fresh nonce is accepted once");
    expect_true(wifi.confirmProvisioning(false, 1) && store.save_calls == 0,
                "user denial discards credentials without persistence");
    expect_true(!wifi.stageProvisioning(payload, length, 2, 1000),
                "duplicate nonce cannot be replayed");
}

static void test_wifi_provisioning_rejects_busy_replay_and_failed_forget() {
    FakeWifiRadio radio;
    FakeWifiCredentialStore store;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power, store);
    const uint8_t nonce_a[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    const uint8_t nonce_b[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    uint8_t first[128]{};
    uint8_t second[128]{};
    const uint16_t first_length = build_wifi_provision_payload(
        first, sizeof(first), 1060, nonce_a, "Network-A", "password-a"
    );
    const uint16_t second_length = build_wifi_provision_payload(
        second, sizeof(second), 1060, nonce_b, "Network-B", "password-b"
    );
    expect_true(wifi.stageProvisioning(first, first_length, 0, 1000),
                "first provisioning request enters the confirmation gate");
    expect_true(!wifi.stageProvisioning(second, second_length, 1, 1000) &&
                    wifi.provisioningSnapshot().status ==
                        firefly::WifiProvisioningStatus::AwaitingUser,
                "a second request cannot overwrite an in-flight credential");
    expect_true(wifi.confirmProvisioning(false, 2),
                "denying the first request clears the busy gate");
    expect_true(wifi.stageProvisioning(second, second_length, 3, 1000),
                "a distinct nonce is accepted after the first request ends");
    expect_true(wifi.confirmProvisioning(false, 4),
                "the second request can be denied independently");
    expect_true(!wifi.stageProvisioning(first, first_length, 5, 1000),
                "A-B-A replay is rejected for the full nonce window");

    expect_true(wifi.provision("Persisted", "secret"),
                "a saved network is available for forget failure coverage");
    store.saved.valid = true;
    strlcpy(store.saved.ssid, "Persisted", sizeof(store.saved.ssid));
    strlcpy(store.saved.password, "secret", sizeof(store.saved.password));
    store.clear_result = false;
    const uint8_t forget_payload[2] = {1, 0};
    expect_true(wifi.stageProvisioning(forget_payload, sizeof(forget_payload),
                                       10, 1000),
                "forget request still requires confirmation");
    expect_true(!wifi.confirmProvisioning(true, 11) &&
                    wifi.provisioningSnapshot().status ==
                        firefly::WifiProvisioningStatus::PersistenceFailed &&
                    store.saved.valid && !wifi.provisioned(),
                "failed durable deletion is reported while RAM secrets are cleared");
}

static void test_wifi_soft_ap_requires_exclusive_idle_radio() {
    FakeWifiRadio radio;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power);
    expect_true(wifi.provision("Firefly Lab", "secret"),
                "station credentials are available");
    expect_true(wifi.request(firefly::WifiPurpose::Weather, 1000),
                "weather owns the station session");
    expect_true(!wifi.beginSoftApSession(firefly::WifiPurpose::Transfer, 1001) &&
                    wifi.active(firefly::WifiPurpose::Weather) &&
                    wifi.mode() == firefly::WifiMode::Connecting,
                "SoftAP cannot replace or strand an active station purpose");
    wifi.release(firefly::WifiPurpose::Weather, 1002);
    const uint8_t station_connects = radio.connect_calls;
    const uint8_t nonce_a[8] = {4, 3, 2, 1, 8, 7, 6, 5};
    uint8_t payload[128]{};
    uint16_t length = build_wifi_provision_payload_v2(
        payload, sizeof(payload), 60, nonce_a, "Replacement", "secret-2");
    expect_true(wifi.stageProvisioning(payload, length, 1003, 0),
                "credential replacement can await confirmation while radio is idle");
    expect_true(wifi.beginSoftApSession(firefly::WifiPurpose::Transfer, 1003),
                "transfer can start SoftAP after the station releases");
    expect_true(!wifi.request(firefly::WifiPurpose::Ntp, 1004) &&
                    !wifi.request(firefly::WifiPurpose::Weather, 1004) &&
                    wifi.mode() == firefly::WifiMode::SoftAp &&
                    wifi.active(firefly::WifiPurpose::Transfer) &&
                    radio.connect_calls == station_connects,
                "station requests cannot preempt or restart an active SoftAP");
    expect_true(!wifi.confirmProvisioning(true, 1005) &&
                    wifi.provisioningSnapshot().status ==
                        firefly::WifiProvisioningStatus::Busy &&
                    wifi.mode() == firefly::WifiMode::SoftAp &&
                    radio.connect_calls == station_connects,
                "confirmed provisioning cannot tear down an active SoftAP");
    wifi.release(firefly::WifiPurpose::Transfer, 1006);

    const uint8_t nonce_b[8] = {5, 3, 2, 1, 8, 7, 6, 4};
    length = build_wifi_provision_payload_v2(
        payload, sizeof(payload), 60, nonce_b, "Replacement", "secret-3");
    expect_true(wifi.stageProvisioning(payload, length, 1007, 0) &&
                    wifi.request(firefly::WifiPurpose::Transfer, 1008),
                "shared-LAN transfer can race a staged confirmation");
    const uint8_t shared_connects = radio.connect_calls;
    expect_true(!wifi.confirmProvisioning(true, 1009) &&
                    wifi.provisioningSnapshot().status ==
                        firefly::WifiProvisioningStatus::Busy &&
                    wifi.mode() == firefly::WifiMode::Connecting &&
                    wifi.active(firefly::WifiPurpose::Transfer) &&
                    radio.connect_calls == shared_connects,
                "confirmed provisioning cannot replace an active shared-LAN session");
    wifi.release(firefly::WifiPurpose::Transfer, 1010);
}

static void test_wifi_inactive_error_state_allows_recovery() {
    FakeWifiRadio radio;
    FakeWifiCredentialStore store;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power, store);
    expect_true(wifi.provision("Broken", "secret") &&
                    wifi.request(firefly::WifiPurpose::Weather, 2000),
                "failed-station recovery begins from a provisioned request");
    radio.link_state = firefly::WifiLinkState::AuthFailed;
    wifi.tick(2001);
    expect_true(wifi.mode() == firefly::WifiMode::Error,
                "station authentication failure remains observable");
    expect_true(wifi.beginSoftApSession(firefly::WifiPurpose::Transfer, 2002) &&
                    wifi.mode() == firefly::WifiMode::SoftAp,
                "inactive Error normalizes before direct SoftAP recovery");
    wifi.release(firefly::WifiPurpose::Transfer, 2003);

    radio.link_state = firefly::WifiLinkState::Connecting;
    expect_true(wifi.request(firefly::WifiPurpose::Weather, 2010),
                "station may be retried after SoftAP recovery");
    radio.link_state = firefly::WifiLinkState::AuthFailed;
    wifi.tick(2011);
    const uint8_t nonce[8] = {9, 1, 2, 3, 4, 5, 6, 7};
    uint8_t payload[128]{};
    const uint16_t length = build_wifi_provision_payload_v2(
        payload, sizeof(payload), 60, nonce, "Replacement", "secret-2");
    expect_true(wifi.stageProvisioning(payload, length, 2012, 0) &&
                    wifi.confirmProvisioning(true, 2013) &&
                    wifi.mode() == firefly::WifiMode::Connecting,
                "inactive Error permits confirmed credential replacement");
    radio.link_state = firefly::WifiLinkState::Connected;
    wifi.tick(2014);
    expect_true(wifi.provisioningSnapshot().status ==
                    firefly::WifiProvisioningStatus::Success,
                "replacement credentials complete normal persistence");

    radio.link_state = firefly::WifiLinkState::Connecting;
    expect_true(wifi.request(firefly::WifiPurpose::Weather, 2020),
                "saved replacement can enter another station attempt");
    radio.link_state = firefly::WifiLinkState::NotFound;
    wifi.tick(2021);
    const uint8_t forget_payload[2] = {1, 0};
    expect_true(wifi.stageProvisioning(forget_payload, sizeof(forget_payload),
                                       2022, 0) &&
                    wifi.confirmProvisioning(true, 2023) &&
                    wifi.provisioningSnapshot().status ==
                        firefly::WifiProvisioningStatus::Forgotten &&
                    !wifi.provisioned(),
                "inactive Error permits confirmed forget recovery");
}

class FakeWeatherCacheStore : public firefly::WeatherCacheStore {
public:
    bool load(firefly::WeatherSnapshot & output,
              firefly::WeatherSource & source) override {
        output = saved;
        source = saved_source;
        return load_result;
    }

    bool save(const firefly::WeatherSnapshot & snapshot,
              firefly::WeatherSource source) override {
        ++save_calls;
        saved = snapshot;
        saved_source = source;
        return save_result;
    }

    firefly::WeatherSnapshot saved{};
    firefly::WeatherSource saved_source = firefly::WeatherSource::None;
    bool load_result = true;
    bool save_result = true;
    uint8_t save_calls = 0;
};

static uint16_t build_weather_payload(uint8_t * output,
                                      size_t capacity,
                                      const char * city,
                                      int16_t current,
                                      int16_t high,
                                      int16_t low,
                                      uint16_t code,
                                      int64_t updated_epoch) {
    const size_t city_length = strlen(city);
    if(city_length == 0 || city_length > 31 ||
       capacity < 18 + city_length) return 0;
    output[0] = 1;
    output[1] = static_cast<uint8_t>(city_length);
    output[2] = static_cast<uint8_t>(code & 0xFF);
    output[3] = static_cast<uint8_t>((code >> 8) & 0xFF);
    const int16_t values[3] = {current, high, low};
    for(uint8_t value = 0; value < 3; ++value) {
        const uint16_t raw = static_cast<uint16_t>(values[value]);
        output[4 + value * 2] = static_cast<uint8_t>(raw & 0xFF);
        output[5 + value * 2] = static_cast<uint8_t>((raw >> 8) & 0xFF);
    }
    for(uint8_t index = 0; index < 8; ++index) {
        output[10 + index] = static_cast<uint8_t>(
            (static_cast<uint64_t>(updated_epoch) >> (index * 8)) & 0xFF
        );
    }
    memcpy(output + 18, city, city_length);
    return static_cast<uint16_t>(18 + city_length);
}

static void test_weather_phone_payload_cache_and_freshness() {
    FakeWeatherCacheStore cache;
    firefly::WeatherService weather(cache);
    uint8_t payload[64]{};
    const uint16_t length = build_weather_payload(
        payload, sizeof(payload), "Shenzhen", 255, 301, 220, 2, 1000
    );
    expect_true(weather.applyPhonePayload(payload, length, 1060),
                "valid phone weather payload is cached");
    expect_true(cache.save_calls == 1 &&
                    weather.source() == firefly::WeatherSource::Phone &&
                    strcmp(weather.snapshot(1060).city, "Shenzhen") == 0,
                "phone weather is the unified preferred source");
    expect_true(weather.freshness(1000 + 3 * 60 * 60) ==
                    firefly::WeatherFreshness::Fresh,
                "weather remains fresh through exactly three hours");
    expect_true(weather.freshness(1001 + 3 * 60 * 60) ==
                    firefly::WeatherFreshness::Stale,
                "weather becomes stale after three hours");
    const firefly::WeatherSnapshot old =
        weather.snapshot(1001 + 24 * 60 * 60);
    expect_true(old.valid && old.stale &&
                    weather.freshness(1001 + 24 * 60 * 60) ==
                        firefly::WeatherFreshness::Old,
                "weather older than twenty four hours remains visibly old");

    payload[4] = 0xB0;
    payload[5] = 0x04;
    expect_true(!weather.applyPhonePayload(payload, length, 1060),
                "extreme phone temperature is rejected");
}

static void test_weather_open_meteo_bounds_and_source_switching() {
    FakeWeatherCacheStore cache;
    firefly::WeatherService weather(cache);
    uint8_t payload[64]{};
    const uint16_t length = build_weather_payload(
        payload, sizeof(payload), "Shenzhen", 255, 301, 220, 2, 1000
    );
    expect_true(weather.applyPhonePayload(payload, length, 1000),
                "phone source staged for priority test");
    const char response[] =
        "{\"current_units\":{\"temperature_2m\":\"degC\","
        "\"weather_code\":\"wmo code\"},"
        "\"current\":{\"temperature_2m\":18.5,\"weather_code\":61},"
        "\"daily_units\":{\"temperature_2m_max\":\"degC\","
        "\"temperature_2m_min\":\"degC\"},"
        "\"daily\":{\"temperature_2m_max\":[21.2,22],"
        "\"temperature_2m_min\":[12.4,13]}}";
    firefly::WeatherSnapshot parsed{};
    expect_true(firefly::WeatherService::parseOpenMeteoJson(
                    response, strlen(response), "Shenzhen", 1200, parsed),
                "bounded Open-Meteo response parses required fields");
    expect_true(parsed.temperature_tenths_c == 185 &&
                    parsed.high_tenths_c == 212 &&
                    parsed.low_tenths_c == 124 && parsed.weather_code == 61,
                "Open-Meteo values convert to unified tenths model");
    expect_true(!weather.applyDirectSnapshot(parsed, 2000),
                "fresh phone weather is not overwritten by direct fallback");
    expect_true(weather.applyDirectSnapshot(parsed, 1001 + 3 * 60 * 60),
                "stale phone weather may switch to direct fallback");
    expect_true(weather.source() == firefly::WeatherSource::Direct,
                "direct fallback source is recorded");
    expect_true(!firefly::WeatherService::parseOpenMeteoJson(
                    "{}", 2, "Shenzhen", 1200, parsed),
                "missing Open-Meteo fields are rejected");
    expect_true(!firefly::WeatherService::parseOpenMeteoJson(
                    response, firefly::WeatherService::kMaxResponseBytes + 1,
                    "Shenzhen", 1200, parsed),
                "responses above eight kilobytes are rejected before parsing");
}

class FakeWeatherDeadlineClock : public firefly::WeatherDeadlineClock {
public:
    uint32_t nowMs() const override { return now_ms; }
    void idle() override { now_ms += idle_step_ms; }
    uint32_t now_ms = 0;
    uint32_t idle_step_ms = 1;
};

class FakeWeatherResponseStream : public firefly::WeatherResponseStream {
public:
    FakeWeatherResponseStream(const char * data,
                              FakeWeatherDeadlineClock & clock,
                              uint32_t byte_interval_ms = 0)
        : data_(data), length_(strlen(data)), clock_(clock),
          byte_interval_ms_(byte_interval_ms) {}
    bool connected() override { return index_ < length_; }
    int available() override {
        return index_ < length_ && clock_.now_ms >= next_byte_ms_ ? 1 : 0;
    }
    int read() override {
        if(available() == 0) return -1;
        const uint8_t value = static_cast<uint8_t>(data_[index_++]);
        next_byte_ms_ = clock_.now_ms + byte_interval_ms_;
        return value;
    }
private:
    const char * data_ = nullptr;
    size_t length_ = 0;
    FakeWeatherDeadlineClock & clock_;
    uint32_t byte_interval_ms_ = 0;
    uint32_t next_byte_ms_ = 0;
    size_t index_ = 0;
};

static void test_weather_http_reader_enforces_absolute_deadline() {
    const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
    char output[32]{};
    size_t output_length = 0;
    int status_code = 0;
    FakeWeatherDeadlineClock fast_clock;
    FakeWeatherResponseStream fast_stream(response, fast_clock);
    expect_true(firefly::WeatherHttpResponseReader::read(
                    fast_stream, fast_clock, 15000, output, sizeof(output),
                    output_length, status_code) &&
                    status_code == 200 && output_length == 2 &&
                    strcmp(output, "{}") == 0,
                "bounded HTTP reader accepts a complete fixed-length body");

    FakeWeatherDeadlineClock slow_clock;
    FakeWeatherResponseStream slow_stream(response, slow_clock, 1000);
    output_length = 0;
    status_code = 0;
    expect_true(!firefly::WeatherHttpResponseReader::read(
                    slow_stream, slow_clock, 15000, output, sizeof(output),
                    output_length, status_code) &&
                    slow_clock.now_ms == 15000,
                "slow-trickle headers cannot refresh the absolute deadline");

    const char chunked_response[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "2\r\n{}\r\n0\r\n\r\n";
    FakeWeatherDeadlineClock chunked_clock;
    FakeWeatherResponseStream chunked_stream(chunked_response, chunked_clock);
    output_length = 0;
    status_code = 0;
    expect_true(firefly::WeatherHttpResponseReader::read(
                    chunked_stream, chunked_clock, 15000, output,
                    sizeof(output), output_length, status_code) &&
                    output_length == 2 && strcmp(output, "{}") == 0,
                "bounded HTTP reader decodes chunked weather bodies");

    const char close_response[] = "HTTP/1.1 200 OK\r\n\r\n{}";
    FakeWeatherDeadlineClock close_clock;
    FakeWeatherResponseStream close_stream(close_response, close_clock);
    output_length = 0;
    status_code = 0;
    expect_true(firefly::WeatherHttpResponseReader::read(
                    close_stream, close_clock, 15000, output, sizeof(output),
                    output_length, status_code) && output_length == 2,
                "bounded HTTP reader accepts a clean close-delimited body");

    const char short_response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n{}";
    FakeWeatherDeadlineClock short_clock;
    FakeWeatherResponseStream short_stream(short_response, short_clock);
    output_length = 0;
    status_code = 0;
    expect_true(!firefly::WeatherHttpResponseReader::read(
                    short_stream, short_clock, 15000, output, sizeof(output),
                    output_length, status_code),
                "short fixed-length weather bodies are rejected");
}

static bool decode_hex_fixture(const char * hex,
                               uint8_t * output,
                               size_t output_length) {
    if(!hex || !output || strlen(hex) != output_length * 2) return false;
    for(size_t index = 0; index < output_length; ++index) {
        const char high = hex[index * 2];
        const char low = hex[index * 2 + 1];
        const auto nibble = [](char value) -> int {
            if(value >= '0' && value <= '9') return value - '0';
            if(value >= 'a' && value <= 'f') return value - 'a' + 10;
            if(value >= 'A' && value <= 'F') return value - 'A' + 10;
            return -1;
        };
        const int upper = nibble(high);
        const int lower = nibble(low);
        if(upper < 0 || lower < 0) return false;
        output[index] = static_cast<uint8_t>((upper << 4) | lower);
    }
    return true;
}

static void test_update_manifest_canonical_signature_and_bounds() {
    const char json[] =
        "{\"schema\":1,\"product\":\"FireflyOS\","
        "\"version\":\"0.1.1\",\"build\":101,\"min_build\":100,"
        "\"size\":4096,"
        "\"sha256\":\"1111111111111111111111111111111111111111111111111111111111111111\","
        "\"signature\":\"8a9e323b85716833dcf9fbe6db68503e7d7f5ca913d6a4e6723d8c0c41bf3eb71b0c0a01519fd092b428941bd00da4914416ab103c23299178c90ee2de1167d6\"}";
    firefly::UpdateManifest manifest{};
    expect_true(firefly::UpdateManifestCodec::parseJson(
                    json, strlen(json), manifest),
                "update manifest parses exact signed fields");
    expect_true(manifest.schema == 1 && manifest.build == 101 &&
                    manifest.min_build == 100 && manifest.size == 4096 &&
                    strcmp(manifest.product, "FireflyOS") == 0 &&
                    strcmp(manifest.version, "0.1.1") == 0,
                "update manifest preserves strongly typed metadata");

    const char canonical_hex[] =
        "46464f5441310000010046697265666c794f5300000000000000302e312e3100"
        "0000000000000000000065000000640000000010000011111111111111111111"
        "1111111111111111111111111111111111111111111111111111";
    uint8_t expected[firefly::UpdateManifestCodec::kCanonicalBytes]{};
    uint8_t actual[firefly::UpdateManifestCodec::kCanonicalBytes]{};
    expect_true(decode_hex_fixture(canonical_hex, expected, sizeof(expected)) &&
                    firefly::UpdateManifestCodec::canonicalize(manifest, actual) &&
                    memcmp(expected, actual, sizeof(actual)) == 0,
                "firmware canonical manifest matches signer byte for byte");
    expect_true(firefly::UpdateManifestCodec::verifySignature(
                    manifest, firefly::kDevelopmentUpdatePublicKey),
                "firmware accepts the P-256 golden signature");

    manifest.build = 102;
    expect_true(!firefly::UpdateManifestCodec::verifySignature(
                    manifest, firefly::kDevelopmentUpdatePublicKey),
                "metadata tampering invalidates the update signature");

    const char duplicate[] =
        "{\"schema\":1,\"product\":\"FireflyOS\",\"version\":\"0.1.1\","
        "\"build\":101,\"build\":102,\"min_build\":100,\"size\":4096,"
        "\"sha256\":\"1111111111111111111111111111111111111111111111111111111111111111\","
        "\"signature\":\"8a9e323b85716833dcf9fbe6db68503e7d7f5ca913d6a4e6723d8c0c41bf3eb71b0c0a01519fd092b428941bd00da4914416ab103c23299178c90ee2de1167d6\"}";
    expect_true(!firefly::UpdateManifestCodec::parseJson(
                    duplicate, strlen(duplicate), manifest),
                "duplicate update manifest fields are rejected");

    const char unknown[] =
        "{\"schema\":1,\"product\":\"FireflyOS\",\"version\":\"0.1.1\","
        "\"build\":101,\"min_build\":100,\"size\":4096,"
        "\"sha256\":\"1111111111111111111111111111111111111111111111111111111111111111\","
        "\"signature\":\"8a9e323b85716833dcf9fbe6db68503e7d7f5ca913d6a4e6723d8c0c41bf3eb71b0c0a01519fd092b428941bd00da4914416ab103c23299178c90ee2de1167d6\","
        "\"endpoint\":\"http://example.invalid\"}";
    expect_true(!firefly::UpdateManifestCodec::parseJson(
                    unknown, strlen(unknown), manifest),
                "unknown update manifest fields are rejected");

    char oversized[firefly::UpdateManifestCodec::kMaxJsonBytes + 2]{};
    memset(oversized, ' ', sizeof(oversized));
    oversized[0] = '{';
    oversized[sizeof(oversized) - 2] = '}';
    expect_true(!firefly::UpdateManifestCodec::parseJson(
                    oversized, sizeof(oversized) - 1, manifest),
                "oversized update manifest JSON is rejected before parsing");
}

class FakeUpdateSource : public firefly::UpdateSource {
public:
    firefly::UpdateIoResult open(const firefly::UpdateManifest &) override {
        ++open_calls;
        position = 0;
        return open_result;
    }
    firefly::UpdateIoResult read(uint8_t * output,
                                 size_t capacity,
                                 size_t & output_length) override {
        ++read_calls;
        if(capacity > max_capacity) max_capacity = capacity;
        output_length = 0;
        if(read_result != firefly::UpdateIoResult::Ok) return read_result;
        if(position >= length) return firefly::UpdateIoResult::End;
        const size_t available = length - position;
        const size_t count = available < capacity ? available : capacity;
        memcpy(output, bytes + position, count);
        position += count;
        output_length = count;
        return firefly::UpdateIoResult::Ok;
    }
    void close() override { ++close_calls; }

    uint8_t bytes[16]{};
    size_t length = 0;
    size_t position = 0;
    size_t max_capacity = 0;
    uint8_t open_calls = 0;
    uint8_t read_calls = 0;
    uint8_t close_calls = 0;
    firefly::UpdateIoResult open_result = firefly::UpdateIoResult::Ok;
    firefly::UpdateIoResult read_result = firefly::UpdateIoResult::Ok;
};

class FakeUpdateWriter : public firefly::UpdateWriter {
public:
    bool begin(uint32_t size) override {
        ++begin_calls;
        declared_size = size;
        return begin_result;
    }
    bool write(const uint8_t *, size_t size) override {
        ++write_calls;
        if(size > max_write) max_write = size;
        written += size;
        return write_result;
    }
    bool finish() override { ++finish_calls; return finish_result; }
    bool selectForNextBoot() override {
        ++select_calls;
        return select_result;
    }
    void abort() override { ++abort_calls; }

    uint32_t declared_size = 0;
    size_t written = 0;
    size_t max_write = 0;
    uint8_t begin_calls = 0;
    uint8_t write_calls = 0;
    uint8_t finish_calls = 0;
    uint8_t select_calls = 0;
    uint8_t abort_calls = 0;
    bool begin_result = true;
    bool write_result = true;
    bool finish_result = true;
    bool select_result = true;
};

static firefly::UpdateManifest signed_four_byte_update() {
    firefly::UpdateManifest manifest{};
    manifest.schema = 1;
    strcpy(manifest.product, "FireflyOS");
    strcpy(manifest.version, "0.1.1");
    manifest.build = 101;
    manifest.min_build = 100;
    manifest.size = 4;
    decode_hex_fixture(
        "9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a",
        manifest.sha256, sizeof(manifest.sha256));
    decode_hex_fixture(
        "2cdee97c59cbdd589e1066610b039753588b0cab68de028643ee565e318e2136"
        "25cb59984584375dc41ca60716f6b31b1e238ff6c6882a74c4e8e172ce26b6f6",
        manifest.ecdsa_p256_signature,
        sizeof(manifest.ecdsa_p256_signature));
    return manifest;
}

static firefly::UpdateRuntimeGate safe_update_gate() {
    firefly::UpdateRuntimeGate gate{};
    gate.battery_valid = true;
    gate.battery_percent = 80;
    return gate;
}

static void test_update_service_gates_streams_and_finalizes_once() {
    firefly::UpdateManifest manifest = signed_four_byte_update();
    firefly::UpdateRuntimeGate gate = safe_update_gate();

    {
        firefly::ResourceGovernor resources;
        FakeUpdateWriter writer;
        FakeUpdateSource source;
        firefly::UpdateService updates(resources, writer, 100);
        gate.battery_percent = 39;
        expect_true(!updates.offer(manifest, source, gate, 1000) &&
                        updates.snapshot().failure ==
                            firefly::UpdateFailure::LowPower,
                    "battery below 40 percent blocks OTA");
        gate.battery_percent = 1;
        gate.charging = true;
        expect_true(updates.offer(manifest, source, gate, 1001),
                    "charging explicitly exempts the OTA battery threshold");
    }

    {
        firefly::ResourceGovernor resources;
        FakeUpdateWriter writer;
        FakeUpdateSource source;
        firefly::UpdateService updates(resources, writer, 100);
        gate = safe_update_gate();
        gate.recording_active = true;
        expect_true(!updates.offer(manifest, source, gate, 2000),
                    "active recording blocks OTA");
        expect_true(updates.snapshot().failure ==
                        firefly::UpdateFailure::AudioBusy,
                    "OTA exposes the exact audio gate");
    }

    {
        firefly::ResourceGovernor resources;
        FakeUpdateWriter writer;
        FakeUpdateSource source;
        source.bytes[0] = 1;
        source.bytes[1] = 2;
        source.bytes[2] = 3;
        source.bytes[3] = 4;
        source.length = 4;
        firefly::UpdateService updates(resources, writer, 100);
        gate = safe_update_gate();
        expect_true(updates.offer(manifest, source, gate, 3000) &&
                        updates.snapshot().state ==
                            firefly::UpdateState::Available &&
                        resources.held(firefly::ResourceKind::Ota),
                    "valid signed update reserves the OTA resource");
        expect_true(updates.start(gate, 3001),
                    "available update starts with a consistent gate snapshot");
        updates.tick(3002);
        expect_true(updates.snapshot().processed == 4 &&
                        writer.written == 4 &&
                        source.max_capacity <= firefly::UpdateService::kChunkBytes &&
                        writer.max_write <= firefly::UpdateService::kChunkBytes,
                    "OTA streams bounded chunks into the inactive writer");
        updates.tick(3003);
        expect_true(updates.snapshot().state == firefly::UpdateState::Writing &&
                        !updates.cancel(3003),
                    "final boot selection phase cannot be cancelled");
        updates.tick(3004);
        const firefly::UpdateSnapshot complete = updates.snapshot();
        expect_true(complete.state == firefly::UpdateState::RebootPending &&
                        complete.progress_percent == 100 &&
                        writer.finish_calls == 1 && writer.select_calls == 1 &&
                        writer.abort_calls == 0 && source.close_calls == 1 &&
                        !resources.held(firefly::ResourceKind::Ota),
                    "verified OTA selects next boot and cleans up exactly once");
        updates.tick(3005);
        expect_true(writer.finish_calls == 1 && writer.select_calls == 1 &&
                        source.close_calls == 1,
                    "OTA terminal state wins and cleanup remains idempotent");
    }

    {
        firefly::ResourceGovernor resources;
        FakeUpdateWriter writer;
        FakeUpdateSource source;
        source.bytes[0] = 9;
        source.bytes[1] = 9;
        source.bytes[2] = 9;
        source.bytes[3] = 9;
        source.length = 4;
        firefly::UpdateService updates(resources, writer, 100);
        gate = safe_update_gate();
        expect_true(updates.offer(manifest, source, gate, 4000) &&
                        updates.start(gate, 4001),
                    "hash mismatch fixture starts normally");
        updates.tick(4002);
        updates.tick(4003);
        expect_true(updates.snapshot().state == firefly::UpdateState::Failed &&
                        updates.snapshot().failure ==
                            firefly::UpdateFailure::HashMismatch &&
                        writer.select_calls == 0 && writer.abort_calls == 1,
                    "hash mismatch aborts and never selects the target slot");
    }
}

static void test_update_sources_are_managed_and_fail_closed() {
    expect_true(firefly::SdUpdateSource::validManagedUpdatePath(
                    "/FireflyOS/Updates/firefly-0.1.1.bin"),
                "SD OTA source accepts one managed update filename");
    expect_true(!firefly::SdUpdateSource::validManagedUpdatePath(
                    "/FireflyOS/Music/firefly.bin") &&
                    !firefly::SdUpdateSource::validManagedUpdatePath(
                    "/FireflyOS/Updates/nested/firefly.bin") &&
                    !firefly::SdUpdateSource::validManagedUpdatePath(
                    "/FireflyOS/Updates/../firefly.bin") &&
                    !firefly::SdUpdateSource::validManagedUpdatePath(
                    "/FireflyOS/Updates/firefly..bin") &&
                    !firefly::SdUpdateSource::validManagedUpdatePath(
                    "/FireflyOS/Updates/firefly.bin.part"),
                "SD OTA source rejects escaped, nested, and partial paths");
    expect_true(!firefly::HttpsUpdateSource::configured(),
                "HTTPS OTA source fails closed when no build endpoint exists");
    expect_true(firefly::HttpsUpdateSource::validFilename("firefly-0.1.1.bin") &&
                    !firefly::HttpsUpdateSource::validFilename("update.json") &&
                    !firefly::HttpsUpdateSource::validFilename("../update.bin") &&
                    !firefly::HttpsUpdateSource::validFilename("update.bin.part"),
                "HTTPS package filenames are one safe final bin component");
    expect_true(firefly::HttpsManifestSource::validFilename("update.json") &&
                    !firefly::HttpsManifestSource::validFilename("update.bin") &&
                    !firefly::HttpsManifestSource::validFilename("../update.json") &&
                    !firefly::HttpsManifestSource::validFilename(
                        "update.json.part"),
                "HTTPS manifest filenames are one safe final json component");
    expect_true(!firefly::HttpsManifestSource::configured() &&
                    firefly::UpdateManifestCodec::kMaxJsonBytes <= 2048,
                "HTTPS manifests fail closed and retain a fixed JSON bound");
}

class FakeUpdateManifestSource : public firefly::UpdateManifestSource {
public:
    firefly::UpdateIoResult fetch(firefly::UpdateManifest & output) override {
        ++fetch_calls;
        output = manifest;
        return result;
    }

    firefly::UpdateManifest manifest{};
    firefly::UpdateIoResult result = firefly::UpdateIoResult::Unavailable;
    uint8_t fetch_calls = 0;
};

static void test_update_coordinator_prefers_sd_and_bounds_commands() {
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    FakeWifiRadio radio;
    firefly::WifiService wifi(radio, power);
    firefly::ResourceGovernor resources;
    FakeUpdateWriter writer;
    FakeUpdateSource sd_package;
    FakeUpdateSource https_package;
    FakeUpdateManifestSource sd_manifest;
    FakeUpdateManifestSource https_manifest;
    firefly::UpdateService updates(resources, writer, 100);
    firefly::UpdateCoordinator coordinator(
        updates, wifi, sd_manifest, sd_package,
        https_manifest, https_package);

    sd_manifest.manifest = signed_four_byte_update();
    sd_manifest.result = firefly::UpdateIoResult::Ok;
    expect_true(coordinator.postCheck(),
                "update coordinator accepts a bounded check command");
    coordinator.runOnce(1000, safe_update_gate());
    expect_true(sd_manifest.fetch_calls == 1 &&
                    https_manifest.fetch_calls == 0 &&
                    radio.connect_calls == 0 &&
                    updates.snapshot().state == firefly::UpdateState::Available,
                "SD manifest wins without starting Wi-Fi or HTTPS");

    FakeUpdateWriter writer2;
    firefly::UpdateService updates2(resources, writer2, 100);
    FakeUpdateManifestSource unavailable_sd;
    firefly::UpdateCoordinator no_endpoint(
        updates2, wifi, unavailable_sd, sd_package,
        https_manifest, https_package);
    expect_true(no_endpoint.postCheck(),
                "fallback discovery check is queued");
    no_endpoint.runOnce(2000, safe_update_gate());
    expect_true(updates2.snapshot().state == firefly::UpdateState::Blocked &&
                    updates2.snapshot().failure ==
                        firefly::UpdateFailure::NoHttpsEndpoint &&
                    radio.connect_calls == 0,
                "Development fails closed before Wi-Fi when HTTPS is absent");

    FakeUpdateWriter writer3;
    firefly::UpdateService updates3(resources, writer3, 100);
    firefly::UpdateCoordinator bounded(
        updates3, wifi, unavailable_sd, sd_package,
        https_manifest, https_package);
    bool first_four = true;
    for(uint8_t index = 0; index <
            firefly::UpdateCoordinator::kCommandCapacity; ++index) {
        first_four = first_four && bounded.postCancel();
    }
    expect_true(first_four && !bounded.postCancel(),
                "update coordinator rejects a fifth queued command");
}

class FakeBootValidationPlatform : public firefly::BootValidationPlatform {
public:
    bool pendingVerify() override { ++pending_calls; return pending; }
    bool markValid() override { ++valid_calls; return valid_result; }
    bool markInvalidAndRollback() override {
        ++rollback_calls;
        return rollback_result;
    }

    bool pending = false;
    bool valid_result = true;
    bool rollback_result = true;
    uint8_t pending_calls = 0;
    uint8_t valid_calls = 0;
    uint8_t rollback_calls = 0;
};

static void submit_all_boot_checks(firefly::BootValidationService & service,
                                   bool value = true) {
    service.submit(firefly::BootCheck::Rtc, value);
    service.submit(firefly::BootCheck::Pmu, value);
    service.submit(firefly::BootCheck::Display, value);
    service.submit(firefly::BootCheck::Touch, value);
    service.submit(firefly::BootCheck::Nvs, value);
    service.submit(firefly::BootCheck::MainUi, value);
}

static void test_boot_validation_is_pending_only_and_bounded() {
    {
        FakeBootValidationPlatform platform;
        firefly::BootValidationService service(platform);
        expect_true(!service.begin(1000) &&
                        service.snapshot().state ==
                            firefly::BootValidationState::Inactive,
                    "normal boot leaves rollback APIs inactive");
        submit_all_boot_checks(service);
        for(uint8_t index = 0; index < 8; ++index) service.tick(1001 + index);
        expect_true(platform.valid_calls == 0 && platform.rollback_calls == 0,
                    "normal boot never marks OTA state");
    }

    {
        FakeBootValidationPlatform platform;
        platform.pending = true;
        firefly::BootValidationService service(platform);
        expect_true(service.begin(2000),
                    "pending-verify image starts the six-check state machine");
        submit_all_boot_checks(service);
        for(uint8_t index = 0; index < 6; ++index) service.tick(2001 + index);
        expect_true(service.snapshot().state ==
                        firefly::BootValidationState::Valid &&
                        platform.valid_calls == 1 && platform.rollback_calls == 0,
                    "six successful checks mark the image valid exactly once");
        service.tick(2010);
        expect_true(platform.valid_calls == 1,
                    "valid terminal state does not call OTA APIs again");
    }

    {
        FakeBootValidationPlatform platform;
        platform.pending = true;
        firefly::BootValidationService service(platform);
        service.begin(3000);
        service.submit(firefly::BootCheck::Rtc, false);
        service.tick(3001);
        expect_true(service.snapshot().state ==
                        firefly::BootValidationState::RollbackRequested &&
                        platform.rollback_calls == 1 && platform.valid_calls == 0,
                    "failed required check requests rollback immediately");
    }

    {
        FakeBootValidationPlatform platform;
        platform.pending = true;
        platform.rollback_result = false;
        firefly::BootValidationService service(platform);
        service.begin(4000);
        service.tick(4000 + firefly::BootValidationService::kDeadlineMs);
        service.tick(4001 + firefly::BootValidationService::kDeadlineMs);
        expect_true(service.snapshot().state ==
                        firefly::BootValidationState::Error &&
                        service.snapshot().failure ==
                            firefly::BootValidationFailure::RollbackApiFailed &&
                        platform.rollback_calls == 1,
                    "rollback API failure becomes a stable non-repeating error");
    }
}

class FakeDiagnosticExport : public firefly::DiagnosticExport {
public:
    bool begin() override { ++begin_calls; return begin_result; }
    bool write(const firefly::DiagnosticRecord & record) override {
        ++write_calls;
        last_timestamp = record.timestamp_ms;
        return write_result;
    }
    bool finish() override { ++finish_calls; return finish_result; }
    void abort() override { ++abort_calls; }

    bool begin_result = true;
    bool write_result = true;
    bool finish_result = true;
    uint8_t begin_calls = 0;
    uint8_t finish_calls = 0;
    uint8_t abort_calls = 0;
    uint16_t write_calls = 0;
    uint32_t last_timestamp = 0;
};

static void test_diagnostics_ring_metrics_and_explicit_export() {
    firefly::DiagnosticService diagnostics;
    firefly::DiagnosticSample sample{};
    sample.internal_free = 100000;
    for(uint32_t index = 0; index < 65; ++index) {
        diagnostics.record(index, firefly::DiagnosticReason::SessionBoundary,
                           sample);
    }
    firefly::DiagnosticRecord oldest{};
    firefly::DiagnosticRecord newest{};
    expect_true(diagnostics.count() == 64 && diagnostics.at(0, oldest) &&
                    diagnostics.at(63, newest) && oldest.timestamp_ms == 1 &&
                    newest.timestamp_ms == 64,
                "diagnostics ring overwrites only the oldest of 65 records");
    expect_true(diagnostics.sampleMinute(1000, sample) &&
                    !diagnostics.sampleMinute(60999, sample) &&
                    diagnostics.sampleMinute(61000, sample),
                "periodic diagnostics records at most once per minute");

    FakeDiagnosticExport failed;
    failed.write_result = false;
    const uint8_t count_before = diagnostics.count();
    expect_true(!diagnostics.exportTo(failed) && failed.abort_calls == 1 &&
                    diagnostics.count() == count_before,
                "failed explicit export preserves the RAM diagnostics ring");

    FakeDiagnosticExport complete;
    expect_true(diagnostics.exportTo(complete) &&
                    complete.write_calls == diagnostics.count() &&
                    complete.finish_calls == 1 && complete.abort_calls == 0,
                "explicit export writes the bounded ring in stable order");

    firefly::EventBus events;
    firefly::SystemEvent event{};
    event.priority = firefly::EventPriority::Normal;
    for(uint8_t index = 0; index < firefly::EventBus::kCapacity; ++index) {
        events.post(event);
    }
    expect_true(!events.post(event) &&
                    events.peakSize() == firefly::EventBus::kCapacity &&
                    events.droppedCount() == 1,
                "event bus exposes bounded peak and saturated drop metrics");
}

class FakeFactoryResetOwners : public firefly::FactoryResetOwners {
public:
    bool clearPairing() override { ++pairing_calls; return pairing_ok; }
    bool clearWifi() override { ++wifi_calls; return wifi_ok; }
    bool clearNotifications() override { ++notification_calls; return true; }
    bool clearWeather() override { ++weather_calls; return true; }
    bool clearSettings() override { ++settings_calls; return settings_ok; }
    bool clearCaches() override { ++cache_calls; return true; }
    bool clearManagedSdRoot() override { ++sd_calls; return sd_ok; }

    uint8_t pairing_calls = 0;
    uint8_t wifi_calls = 0;
    uint8_t notification_calls = 0;
    uint8_t weather_calls = 0;
    uint8_t settings_calls = 0;
    uint8_t cache_calls = 0;
    uint8_t sd_calls = 0;
    bool pairing_ok = true;
    bool wifi_ok = true;
    bool settings_ok = true;
    bool sd_ok = true;
};

class FakeFactoryResetRebooter : public firefly::FactoryResetRebooter {
public:
    bool requestReboot() override {
        ++calls;
        if(reentered_service) reentered_generation = reentered_service->beginRequest();
        return result;
    }
    uint8_t calls = 0;
    bool result = true;
    firefly::FactoryResetService * reentered_service = nullptr;
    uint32_t reentered_generation = 0;
};

static void test_factory_reset_defaults_to_keep_sd_and_requires_generation() {
    {
        FakeFactoryResetOwners owners;
        FakeFactoryResetRebooter rebooter;
        firefly::FactoryResetService reset(owners, rebooter);
        const uint32_t generation = reset.beginRequest();
        expect_true(generation != 0 && reset.confirmInternal(generation) &&
                        reset.execute(false),
                    "internal factory reset executes after first confirmation");
        expect_true(owners.pairing_calls == 1 && owners.wifi_calls == 1 &&
                        owners.notification_calls == 1 &&
                        owners.weather_calls == 1 && owners.settings_calls == 1 &&
                        owners.cache_calls == 1 && owners.sd_calls == 0 &&
                        rebooter.calls == 1,
                    "factory reset clears internal owners and keeps SD by default");
    }

    {
        FakeFactoryResetOwners owners;
        FakeFactoryResetRebooter rebooter;
        firefly::FactoryResetService reset(owners, rebooter);
        const uint32_t generation = reset.beginRequest();
        expect_true(reset.confirmInternal(generation) &&
                        !reset.confirmSdErase(generation + 1) &&
                        !reset.execute(true) && owners.sd_calls == 0,
                    "stale SD confirmation generation cannot erase managed data");
        expect_true(reset.confirmSdErase(generation) && reset.execute(true) &&
                        owners.sd_calls == 1,
                    "current one-shot generation permits explicitly confirmed SD erase");
    }

    {
        FakeFactoryResetOwners owners;
        owners.settings_ok = false;
        FakeFactoryResetRebooter rebooter;
        firefly::FactoryResetService reset(owners, rebooter);
        const uint32_t generation = reset.beginRequest();
        reset.confirmInternal(generation);
        expect_true(!reset.execute(false) && rebooter.calls == 0 &&
                        reset.snapshot().state == firefly::FactoryResetState::Failed &&
                        reset.snapshot().failure ==
                            firefly::FactoryResetFailure::InternalClearFailed,
                    "critical clear failure never reboots or reports completion");
        const firefly::FactoryResetSnapshot terminal = reset.snapshot();
        expect_true(!reset.execute(false) &&
                        reset.snapshot().state == terminal.state &&
                        reset.snapshot().failure == terminal.failure &&
                        reset.snapshot().generation == terminal.generation,
                    "a repeated execute cannot overwrite the first terminal result");
    }

    {
        FakeFactoryResetOwners owners;
        FakeFactoryResetRebooter rebooter;
        firefly::FactoryResetService reset(owners, rebooter);
        const uint32_t generation = reset.beginRequest();
        expect_true(reset.confirmInternal(generation),
                    "reentrant reset test reaches its confirmation gate");
        rebooter.reentered_service = &reset;
        expect_true(!reset.execute(false) && rebooter.reentered_generation != 0,
                    "a newer request invalidates the older reboot completion");
        const firefly::FactoryResetSnapshot current = reset.snapshot();
        expect_true(current.state == firefly::FactoryResetState::Preview &&
                        current.generation == rebooter.reentered_generation &&
                        current.failure == firefly::FactoryResetFailure::None,
                    "reboot callback cannot overwrite a newer reset generation");
    }
}

static void test_hardware_capabilities_degrade_independently() {
    firefly::HardwareCapabilities capabilities;
    for(uint8_t index = 0;
        index < static_cast<uint8_t>(firefly::HardwareDevice::Count);
        ++index) {
        capabilities.set(
            static_cast<firefly::HardwareDevice>(index),
            firefly::HardwareAvailability::Available,
            firefly::HardwareFailure::None);
    }

    capabilities.set(firefly::HardwareDevice::Rtc,
                     firefly::HardwareAvailability::Unavailable,
                     firefly::HardwareFailure::NotDetected);
    capabilities.set(firefly::HardwareDevice::Sd,
                     firefly::HardwareAvailability::Degraded,
                     firefly::HardwareFailure::IoFailure);
    const firefly::HardwareCapabilitySnapshot snapshot = capabilities.snapshot();
    expect_true(
        snapshot.status[static_cast<uint8_t>(firefly::HardwareDevice::Rtc)] ==
            firefly::HardwareAvailability::Unavailable &&
        snapshot.failure[static_cast<uint8_t>(firefly::HardwareDevice::Rtc)] ==
            firefly::HardwareFailure::NotDetected,
        "RTC failure preserves explicit hardware failure code");
    expect_true(
        snapshot.status[static_cast<uint8_t>(firefly::HardwareDevice::Sd)] ==
            firefly::HardwareAvailability::Degraded &&
        capabilities.available(firefly::HardwareDevice::Sd),
        "degraded SD remains explicitly usable");
    expect_true(capabilities.available(firefly::HardwareDevice::Pmu) &&
                    capabilities.available(firefly::HardwareDevice::Imu) &&
                    capabilities.available(firefly::HardwareDevice::Codec) &&
                    capabilities.available(firefly::HardwareDevice::Ble) &&
                    capabilities.available(firefly::HardwareDevice::Wifi),
                "one device failure does not disable unrelated capabilities");

    firefly::NavigationController navigation;
    expect_true(navigation.current() == firefly::Route::Lock &&
                    navigation.open(firefly::Route::Home) &&
                    navigation.current() == firefly::Route::Home,
                "hardware degradation does not block Lock or Home navigation");
}

class FakeBulkStorage : public firefly::BulkTransferStorage {
public:
    bool beginSession() override { ++session_starts; return session_result; }
    void endSession() override { ++session_ends; }
    bool cardPresent() const override { return card_present; }
    bool cardAvailable() const override { return card_available; }
    uint64_t freeBytes() const override { return free_bytes; }
    bool beginPart(const char * final_path, uint64_t declared_size) override {
        ++begin_calls;
        strlcpy(path, final_path ? final_path : "", sizeof(path));
        expected_size = declared_size;
        length = 0;
        return begin_result;
    }
    bool append(const uint8_t * data, size_t size) override {
        if(!append_result || length + size > sizeof(bytes)) return false;
        memcpy(bytes + length, data, size);
        length += size;
        return true;
    }
    bool closePart() override { return close_result; }
    bool commitPart() override { ++commit_calls; return commit_result; }
    void removePart() override { ++remove_calls; length = 0; }

    bool card_present = true;
    bool card_available = true;
    uint64_t free_bytes = 128ULL * 1024ULL * 1024ULL;
    bool begin_result = true;
    bool session_result = true;
    bool append_result = true;
    bool close_result = true;
    bool commit_result = true;
    uint8_t bytes[2048]{};
    size_t length = 0;
    uint64_t expected_size = 0;
    uint8_t begin_calls = 0;
    uint8_t commit_calls = 0;
    uint8_t remove_calls = 0;
    uint8_t session_starts = 0;
    uint8_t session_ends = 0;
    char path[192]{};
};

class FakeBulkTransport : public firefly::BulkTransferTransport {
public:
    bool startLan(firefly::BulkTransferSink & sink,
                  const char * token_hex) override {
        ++lan_calls;
        active_sink = &sink;
        strlcpy(token, token_hex, sizeof(token));
        return start_result;
    }
    bool startSoftAp(firefly::BulkTransferSink & sink,
                     const char * ssid,
                     const char * password,
                     const char * token_hex) override {
        ++ap_calls;
        active_sink = &sink;
        strlcpy(ap_ssid, ssid, sizeof(ap_ssid));
        strlcpy(ap_password, password, sizeof(ap_password));
        strlcpy(token, token_hex, sizeof(token));
        return start_result;
    }
    void poll(uint32_t now_ms) override { last_poll = now_ms; }
    void stop() override { ++stop_calls; active_sink = nullptr; }
    const char * endpoint() const override { return "http://192.168.4.1/upload"; }

    firefly::BulkTransferSink * active_sink = nullptr;
    bool start_result = true;
    uint8_t lan_calls = 0;
    uint8_t ap_calls = 0;
    uint8_t stop_calls = 0;
    uint32_t last_poll = 0;
    char token[33]{};
    char ap_ssid[24]{};
    char ap_password[24]{};
};

static void deterministic_bulk_random(uint8_t * output, size_t length) {
    for(size_t index = 0; index < length; ++index) {
        output[index] = static_cast<uint8_t>(index + 1);
    }
}

static void configure_bulk_request(firefly::BulkTransferRequest & request,
                                   uint16_t request_id,
                                   const char * path,
                                   uint64_t declared_size,
                                   const uint8_t digest[32],
                                   bool prefer_shared_lan = false) {
    request = {};
    request.request_id = request_id;
    request.prefer_shared_lan = prefer_shared_lan;
    request.declared_size = declared_size;
    strlcpy(request.managed_path, path, sizeof(request.managed_path));
    memcpy(request.expected_sha256, digest, 32);
}

static void test_bulk_transfer_token_path_hash_and_atomic_commit() {
    FakeBulkStorage storage;
    FakeBulkTransport transport;
    FakeWifiRadio radio;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power);
    firefly::BulkTransferService bulk(storage, transport, power, wifi,
                                       deterministic_bulk_random);
    uint8_t data[1024]{};
    for(size_t index = 0; index < sizeof(data); ++index) {
        data[index] = static_cast<uint8_t>(index & 0xFF);
    }
    uint8_t digest[32]{};
    mbedtls_sha256_ret(data, sizeof(data), digest, 0);
    firefly::BulkTransferRequest request{};
    configure_bulk_request(request, 10, "/FireflyOS/Pictures/test.bin",
                           sizeof(data), digest);
    request.audio_active = false;
    request.ota_active = false;
    expect_true(bulk.startSession(request, 1000),
                "safe transfer request starts temporary soft ap");
    expect_true(power.wifiSessionActive() &&
                    bulk.snapshot().state ==
                        firefly::BulkTransferState::WaitingForNetwork,
                "accepted SoftAP transfer blocks sleep before worker startup");
    bulk.tick(1000);
    firefly::BulkTransferSnapshot session = bulk.snapshot();
    expect_true(session.state == firefly::BulkTransferState::Ready &&
                    session.request_id == 10 &&
                    session.result_request_id == 10 &&
                    session.result_generation >= 2 &&
                    strlen(session.token_hex) == 32 && transport.ap_calls == 1,
                "transfer exposes one bounded authenticated endpoint");

    expect_true(!bulk.beginFile(session.token_hex, "../secret.bin",
                                sizeof(data), digest, 1010) &&
                    bulk.snapshot().state == firefly::BulkTransferState::Error &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::InvalidPath,
                "directory traversal terminates with an exact correlated result");
    bulk.tick(1011);
    configure_bulk_request(request, 11, "/FireflyOS/Pictures/test.bin",
                           sizeof(data), digest);
    expect_true(bulk.startSession(request, 1012),
                "a new transfer may start after invalid metadata cleanup");
    bulk.tick(1012);
    session = bulk.snapshot();
    expect_true(bulk.beginFile(session.token_hex,
                               "/FireflyOS/Pictures/test.bin",
                               sizeof(data), digest, 1020),
                "allowed managed path begins a part file");
    expect_true(bulk.writeChunk(session.token_hex, data, 600, 1030) &&
                    bulk.writeChunk(session.token_hex, data + 600, 424, 1040),
                "bounded chunks stream without whole-file buffering");
    expect_true(bulk.finishFile(session.token_hex, 1050) &&
                    storage.commit_calls == 1 &&
                    strcmp(storage.path,
                           "/FireflyOS/Pictures/test.bin") == 0,
                 "matching size and sha commit the part atomically");
    expect_true(!bulk.cancelSession(11, 1051) &&
                    bulk.snapshot().state ==
                        firefly::BulkTransferState::Completed &&
                    bulk.snapshot().result_state ==
                        firefly::BulkTransferState::Completed,
                "late cancellation cannot relabel an already committed file");
    bulk.cancel(firefly::BulkTransferFailure::LowPower, 1052);
    expect_true(bulk.snapshot().state ==
                    firefly::BulkTransferState::Completed &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::None,
                "post-commit resource shutdown preserves the completed result");
    bulk.tick(2051);
    configure_bulk_request(request, 12, "/FireflyOS/Pictures/next.bin",
                           sizeof(data), digest);
    expect_true(bulk.startSession(request, 2052),
                "a completed transfer allows a new session after cleanup");
    bulk.cancelSession(12, 2053);
    bulk.tick(2053);
}

static void test_bulk_transfer_rejects_bad_hash_and_cleans_timeout() {
    FakeBulkStorage storage;
    FakeBulkTransport transport;
    FakeWifiRadio radio;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power);
    firefly::BulkTransferService bulk(storage, transport, power, wifi,
                                       deterministic_bulk_random);
    firefly::BulkTransferRequest request{};
    uint8_t wrong_digest[32]{};
    configure_bulk_request(request, 11, "/FireflyOS/Music/bad.bin", 3,
                           wrong_digest);
    expect_true(bulk.startSession(request, 2000),
                "second temporary transfer starts");
    bulk.tick(2000);
    firefly::BulkTransferSnapshot session = bulk.snapshot();
    const uint8_t data[3] = {'a', 'b', 'c'};
    expect_true(bulk.beginFile(session.token_hex,
                               "/FireflyOS/Music/bad.bin", 3,
                               wrong_digest, 2010) &&
                    bulk.writeChunk(session.token_hex, data, sizeof(data), 2020),
                "bad hash transfer reaches verification");
    expect_true(!bulk.finishFile(session.token_hex, 2030) &&
                    storage.commit_calls == 0,
                "bad hash deletes only part file and never commits");
    bulk.tick(2030);
    expect_true(storage.remove_calls == 1,
                "bulk task removes a failed part after the callback returns");

    configure_bulk_request(request, 12, "/FireflyOS/Music/retry.bin", 3,
                           wrong_digest);
    expect_true(bulk.startSession(request, 2500),
                "overrun regression starts a fresh bounded session");
    bulk.tick(2500);
    session = bulk.snapshot();
    const uint8_t overrun[4] = {'a', 'b', 'c', 'd'};
    expect_true(bulk.beginFile(session.token_hex,
                               "/FireflyOS/Music/retry.bin", 3,
                               wrong_digest, 2510) &&
                    !bulk.writeChunk(session.token_hex, overrun,
                                     sizeof(overrun), 2520) &&
                    bulk.snapshot().state == firefly::BulkTransferState::Error &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::SizeMismatch,
                "over-declared HTTP chunks terminate with SizeMismatch");
    bulk.tick(2520);

    configure_bulk_request(request, 13, "/FireflyOS/Music/retry.bin", 3,
                           wrong_digest);
    expect_true(bulk.startSession(request, 3000),
                "session may restart after a finite failure");
    bulk.tick(3000);
    bulk.tick(3000 + firefly::BulkTransferService::kIdleTimeoutMs + 1);
    expect_true(bulk.snapshot().state == firefly::BulkTransferState::Cancelled &&
                    transport.stop_calls >= 2 && wifi.mode() == firefly::WifiMode::Off,
                "idle timeout removes partial state and powers wifi off");
}

static void test_bulk_transfer_rolls_back_failed_soft_ap_start() {
    FakeBulkStorage storage;
    FakeBulkTransport transport;
    transport.start_result = false;
    FakeWifiRadio radio;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power);
    firefly::BulkTransferService bulk(storage, transport, power, wifi,
                                       deterministic_bulk_random);
    firefly::BulkTransferRequest request{};
    uint8_t digest[32]{};
    configure_bulk_request(request, 13, "/FireflyOS/Updates/update.bin", 1,
                           digest);
    expect_true(bulk.startSession(request, 4000),
                "temporary transfer is queued for the bulk task");
    bulk.tick(4000);
    expect_true(bulk.snapshot().state == firefly::BulkTransferState::Error &&
                    wifi.mode() == firefly::WifiMode::Off &&
                    storage.session_starts == 1 && storage.session_ends == 1 &&
                    transport.stop_calls == 1,
                "failed SoftAP transport rolls back Wi-Fi and SD lease");
}

static void test_bulk_transfer_preflight_and_explicit_busy_result() {
    FakeBulkStorage storage;
    FakeBulkTransport transport;
    FakeWifiRadio radio;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power);
    firefly::BulkTransferService bulk(storage, transport, power, wifi,
                                      deterministic_bulk_random);
    uint8_t digest[32]{};
    firefly::BulkTransferRequest request{};
    configure_bulk_request(request, 20, "../escape.bin", 1024, digest);
    expect_true(!bulk.startSession(request, 1000) &&
                    bulk.snapshot().result_request_id == 20 &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::InvalidPath &&
                    bulk.snapshot().token_hex[0] == '\0' &&
                    storage.session_starts == 0,
                "invalid negotiated path fails before token or SD lease");

    configure_bulk_request(request, 25,
                           "/FireflyOS/Pictures/album/nested.bin",
                           1024, digest);
    expect_true(!bulk.startSession(request, 1001) &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::InvalidPath,
                "negotiated paths are direct children of managed roots");
    configure_bulk_request(request, 26, "/FireflyOS/Pictures/final.part",
                           1024, digest);
    expect_true(!bulk.startSession(request, 1002) &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::InvalidPath,
                "reserved part suffix cannot become a committed user file");

    configure_bulk_request(request, 21, "/FireflyOS/Pictures/full.bin",
                           1024, digest);
    storage.card_present = false;
    expect_true(!bulk.startSession(request, 1050) &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::SdUnavailable &&
                    storage.session_starts == 0,
                "missing SD is reported before trying to acquire a lease");
    storage.card_present = true;
    storage.session_result = false;
    expect_true(!bulk.startSession(request, 1060) &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::Busy &&
                    storage.session_starts == 1 &&
                    storage.session_ends == 0,
                "open normal SD handles make the bulk lease explicitly busy");
    storage.session_result = true;
    storage.free_bytes = 1024 +
        firefly::BulkTransferService::kSpaceReserveBytes - 1;
    expect_true(!bulk.startSession(request, 1100) &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::InsufficientSpace &&
                    storage.session_starts == 2 && storage.session_ends == 1 &&
                    transport.ap_calls == 0,
                "insufficient space fails before endpoint creation");

    storage.free_bytes = 128ULL * 1024ULL * 1024ULL;
    configure_bulk_request(request, 22, "/FireflyOS/Pictures/active.bin",
                           1024, digest);
    expect_true(bulk.startSession(request, 1200),
                "valid preflight creates one active session");
    const uint32_t accepted_generation = bulk.snapshot().result_generation;
    configure_bulk_request(request, 23, "/FireflyOS/Pictures/busy.bin",
                           1024, digest);
    expect_true(!bulk.startSession(request, 1201) &&
                    bulk.snapshot().state ==
                        firefly::BulkTransferState::WaitingForNetwork &&
                    bulk.snapshot().result_request_id == 23 &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::Busy &&
                    bulk.snapshot().result_generation > accepted_generation,
                 "Busy command emits a new correlated result without replacing session");

    expect_true(!bulk.cancelSession(23, 1202) &&
                    bulk.snapshot().state ==
                        firefly::BulkTransferState::WaitingForNetwork &&
                    bulk.snapshot().result_request_id == 23 &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::Busy,
                "stale cancellation is correlated without replacing session");
    expect_true(bulk.cancelSession(22, 1203) &&
                    bulk.snapshot().state ==
                        firefly::BulkTransferState::Cancelled &&
                    bulk.snapshot().result_request_id == 22 &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::Cancelled,
                "matching cancellation terminates exactly one session");
    const uint32_t cancelled_generation = bulk.snapshot().result_generation;
    bulk.cancel(firefly::BulkTransferFailure::Disconnected, 1204);
    expect_true(bulk.snapshot().result_generation == cancelled_generation &&
                    bulk.snapshot().failure ==
                        firefly::BulkTransferFailure::Cancelled,
                "late HTTP abort preserves an earlier user cancellation");
    bulk.tick(1203);

    configure_bulk_request(request, 28, "/FireflyOS/Pictures/race.bin",
                           16, digest);
    expect_true(bulk.startSession(request, 1500),
                "reverse cancellation order starts a fresh session");
    bulk.cancel(firefly::BulkTransferFailure::Disconnected, 1501);
    const uint32_t disconnected_generation = bulk.snapshot().result_generation;
    expect_true(!bulk.cancelSession(28, 1502) &&
                    bulk.snapshot().result_generation > disconnected_generation &&
                    bulk.snapshot().result_request_id == 28 &&
                    bulk.snapshot().result_state ==
                        firefly::BulkTransferState::Cancelled &&
                    bulk.snapshot().result_failure ==
                        firefly::BulkTransferFailure::Disconnected &&
                    bulk.snapshot().failure ==
                        firefly::BulkTransferFailure::Disconnected,
                "late BLE cancel re-acknowledges the earlier terminal result");
    bulk.tick(1502);

    const uint32_t hard_start = 2000;
    configure_bulk_request(request, 24, "/FireflyOS/Pictures/limited.bin",
                           16, digest);
    expect_true(bulk.startSession(request, hard_start),
                "hard-limit session starts after cancellation cleanup");
    bulk.tick(hard_start);
    const firefly::BulkTransferSnapshot hard_session = bulk.snapshot();
    expect_true(bulk.beginFile(hard_session.token_hex,
                               "/FireflyOS/Pictures/limited.bin", 16,
                               digest, hard_start + 10),
                "hard-limit transfer starts receiving");
    const uint8_t byte = 0;
    for(uint8_t interval = 1; interval <= 3; ++interval) {
        const uint32_t active_at = hard_start +
            interval * 4UL * 60UL * 1000UL;
        bulk.tick(active_at);
        expect_true(bulk.writeChunk(hard_session.token_hex, &byte, 1,
                                    active_at),
                    "periodic data refreshes only the idle deadline");
    }
    bulk.tick(hard_start + firefly::BulkTransferService::kSessionLimitMs);
    expect_true(bulk.snapshot().state ==
                    firefly::BulkTransferState::Cancelled &&
                    bulk.snapshot().failure ==
                        firefly::BulkTransferFailure::Timeout,
                "absolute fifteen-minute limit wins despite recent data");
}

static void test_bulk_transfer_shared_lan_fallback_and_link_loss() {
    FakeBulkStorage storage;
    FakeBulkTransport transport;
    FakeWifiRadio radio;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power);
    expect_true(wifi.provision("Firefly Lab", "secret"),
                "shared-LAN fallback starts with saved credentials");
    firefly::BulkTransferService bulk(storage, transport, power, wifi,
                                      deterministic_bulk_random);
    uint8_t digest[32]{};
    firefly::BulkTransferRequest request{};
    configure_bulk_request(request, 27, "/FireflyOS/Pictures/fallback.bin",
                           16, digest, true);
    expect_true(bulk.startSession(request, 1000) &&
                    wifi.mode() == firefly::WifiMode::Connecting,
                "preferred shared LAN begins with station association");
    wifi.tick(1000 + firefly::WifiService::kConnectionTimeoutMs);
    bulk.tick(1000 + firefly::WifiService::kConnectionTimeoutMs);
    expect_true(bulk.snapshot().state == firefly::BulkTransferState::Ready &&
                    transport.ap_calls == 1 &&
                    wifi.mode() == firefly::WifiMode::SoftAp,
                "station timeout falls back after Wi-Fi clears its purpose");

    wifi.release(firefly::WifiPurpose::Transfer, 17000);
    bulk.tick(17000);
    expect_true(bulk.snapshot().state == firefly::BulkTransferState::Error &&
                    bulk.snapshot().failure ==
                        firefly::BulkTransferFailure::NetworkUnavailable,
                "active transport link loss is reported as unavailable, not timeout");
}

static void test_wifi_session_timeout_and_release() {
    FakeWifiRadio radio;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);

    firefly::WifiService wifi(radio, power);
    wifi.configureTimeout(60000);
    expect_true(wifi.provision("Firefly Lab", "secret"),
                "Wi-Fi accepts bounded credentials");
    expect_true(wifi.request(firefly::WifiPurpose::Weather, 1000),
                "weather requests Wi-Fi");
    expect_true(wifi.mode() == firefly::WifiMode::Connecting &&
                    radio.connect_calls == 1 && power.wifiSessionActive(),
                "Wi-Fi enters connecting state and blocks sleep");
    radio.link_state = firefly::WifiLinkState::Connected;
    wifi.onConnected(5000);
    expect_true(wifi.mode() == firefly::WifiMode::Connected &&
                    radio.power_save_calls == 1,
                "Wi-Fi connected idle uses minimum modem power save");
    wifi.tick(65001);
    expect_true(wifi.mode() == firefly::WifiMode::Off &&
                    radio.power_off_calls == 1 &&
                    !power.wifiSessionActive(),
                "ordinary Wi-Fi session auto stops after idle timeout");

    expect_true(wifi.request(firefly::WifiPurpose::Ntp, 70000),
                "NTP can start a new session");
    wifi.release(firefly::WifiPurpose::Ntp, 70001);
    expect_true(wifi.mode() == firefly::WifiMode::Off &&
                    radio.power_off_calls == 2,
                "last released purpose powers Wi-Fi off immediately");
}

static void test_wifi_connection_and_long_session_limits() {
    FakeWifiRadio radio;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 80;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power);
    expect_true(wifi.provision("Firefly Lab", "secret"),
                "Wi-Fi credentials are available");

    expect_true(wifi.request(firefly::WifiPurpose::Weather, 1000),
                "weather connection starts");
    wifi.tick(16001);
    expect_true(wifi.mode() == firefly::WifiMode::Off,
                "Wi-Fi connection attempt stops after fifteen seconds");

    expect_true(wifi.request(firefly::WifiPurpose::Transfer, 20000),
                "transfer session starts");
    radio.link_state = firefly::WifiLinkState::Connected;
    wifi.onConnected(21000);
    wifi.tick(920001);
    expect_true(wifi.mode() == firefly::WifiMode::Off,
                "transfer session has a fifteen minute hard limit");

    expect_true(wifi.beginSoftApSession(firefly::WifiPurpose::Transfer,
                                        1000000),
                "transfer can start an exclusive SoftAP session");
    wifi.tick(1000000 + firefly::WifiService::kLongSessionLimitMs + 1);
    expect_true(wifi.mode() == firefly::WifiMode::Off &&
                    !wifi.active(firefly::WifiPurpose::Transfer),
                "SoftAP transfer obeys the same fifteen minute hard limit");
}

static void test_wifi_power_policy_rejects_unsafe_sessions() {
    FakeWifiRadio radio;
    firefly::PowerService power;
    firefly::BatteryState battery{};
    battery.valid = true;
    battery.percent = 15;
    power.setBatteryState(battery);
    firefly::WifiService wifi(radio, power);
    expect_true(wifi.provision("Firefly Lab", "secret"),
                "Wi-Fi credentials provisioned for power policy test");

    expect_true(!wifi.request(firefly::WifiPurpose::Transfer, 0) &&
                    !wifi.request(firefly::WifiPurpose::Ota, 0),
                "the inclusive low-battery boundary rejects transfer and OTA");
    expect_true(wifi.request(firefly::WifiPurpose::Weather, 0),
                "low battery still permits a short weather session");
    wifi.release(firefly::WifiPurpose::Weather, 1);

    battery.percent = 5;
    power.setBatteryState(battery);
    expect_true(!wifi.request(firefly::WifiPurpose::Ntp, 2) &&
                    !wifi.request(firefly::WifiPurpose::Weather, 2),
                "the inclusive critical boundary rejects every new Wi-Fi session");

    battery.charging = true;
    battery.vbus_present = true;
    power.setBatteryState(battery);
    expect_true(!wifi.request(firefly::WifiPurpose::Ntp, 3) &&
                    !wifi.request(firefly::WifiPurpose::Transfer, 3),
                "critical battery rejects Wi-Fi even while charging");

    battery = {};
    power.setBatteryState(battery);
    expect_true(!wifi.request(firefly::WifiPurpose::Transfer, 4) &&
                    wifi.request(firefly::WifiPurpose::Weather, 4),
                "unknown battery telemetry rejects high-power Wi-Fi only");
    wifi.release(firefly::WifiPurpose::Weather, 5);

    battery.valid = true;
    battery.percent = -1;
    battery.vbus_present = true;
    power.setBatteryState(battery);
    expect_true(wifi.request(firefly::WifiPurpose::Ntp, 6) &&
                    !wifi.request(firefly::WifiPurpose::Transfer, 6),
                "partial battery telemetry is unknown, not critical, even on VBUS");
    wifi.release(firefly::WifiPurpose::Ntp, 7);
}

static void test_debounced_button_short_and_long_press() {
    firefly::DebouncedButton button;
    expect_true(button.update(false, 0) == firefly::ButtonAction::None,
                "button starts released");
    expect_true(button.update(true, 10) == firefly::ButtonAction::None,
                "button press waits for debounce");
    expect_true(button.update(true, 40) == firefly::ButtonAction::None,
                "button accepts stable press without early action");
    expect_true(button.update(false, 50) == firefly::ButtonAction::None,
                "button release waits for debounce");
    expect_true(button.update(false, 80) == firefly::ButtonAction::ShortPress,
                "button emits short press on stable release");

    expect_true(button.update(true, 100) == firefly::ButtonAction::None,
                "second press begins");
    expect_true(button.update(true, 130) == firefly::ButtonAction::None,
                "second press debounces");
    expect_true(button.update(true, 1129) == firefly::ButtonAction::None,
                "long press waits for threshold");
    expect_true(button.update(true, 1130) == firefly::ButtonAction::LongPress,
                "long press fires at one second");
    expect_true(button.update(true, 1200) == firefly::ButtonAction::None,
                "long press fires only once");
    expect_true(button.update(false, 1210) == firefly::ButtonAction::None,
                "long press release waits for debounce");
    expect_true(button.update(false, 1240) == firefly::ButtonAction::None,
                "long press release does not emit short press");
}

static void test_debounced_button_rejects_jitter() {
    firefly::DebouncedButton button;
    expect_true(button.update(false, 0) == firefly::ButtonAction::None,
                "jitter test starts released");
    expect_true(button.update(true, 10) == firefly::ButtonAction::None,
                "jitter press begins");
    expect_true(button.update(false, 29) == firefly::ButtonAction::None,
                "sub debounce jitter release is ignored");
    expect_true(button.update(false, 60) == firefly::ButtonAction::None,
                "jitter never becomes stable press");
}

static uint8_t sleep_prepare_calls = 0;
static uint8_t sleep_restore_calls = 0;

static bool fake_sleep_prepare() {
    ++sleep_prepare_calls;
    return true;
}

static void fake_sleep_restore() {
    ++sleep_restore_calls;
}

static void test_light_sleep_requires_verified_wake_matrix() {
    firefly::PowerService power;
    power.setSleepHooks({fake_sleep_prepare, fake_sleep_restore});
    expect_true(!power.canEnterLightSleep(),
                "light sleep is disabled without wake verification");
    expect_true(!power.prepareForLightSleep(),
                "sleep prepare hook is gated before verification");

    power.recordWakeVerification(firefly::WakeSource::Boot, 100, 100);
    power.recordWakeVerification(firefly::WakeSource::PowerButton, 100, 100);
    power.recordWakeVerification(firefly::WakeSource::RtcAlarm, 100, 99);
    expect_true(!power.canEnterLightSleep(),
                "one failed wake attempt keeps light sleep disabled");

    power.recordWakeVerification(firefly::WakeSource::RtcAlarm, 100, 100);
    expect_true(power.canEnterLightSleep(),
                "required wake sources enable gate only at one hundred percent");
    expect_true(power.prepareForLightSleep(),
                "verified sleep runs prepare hook");
    power.restoreFromLightSleep();
    expect_true(sleep_prepare_calls == 1 && sleep_restore_calls == 1,
                "sleep hooks run once around verified lifecycle");
}

static bool fake_sleep_enter_result = true;
static uint8_t fake_sleep_enter_calls = 0;

static bool fake_sleep_enter() {
    ++fake_sleep_enter_calls;
    return fake_sleep_enter_result;
}

static void test_light_sleep_attempt_restores_on_success_and_failure() {
    sleep_prepare_calls = 0;
    sleep_restore_calls = 0;
    fake_sleep_enter_calls = 0;
    firefly::PowerService power;
    power.setSleepHooks({fake_sleep_prepare, fake_sleep_restore});
    expect_true(power.attemptLightSleep(fake_sleep_enter) ==
                    firefly::SleepAttemptResult::GateClosed,
                "sleep attempt rejects unverified wake matrix");
    expect_true(fake_sleep_enter_calls == 0,
                "closed sleep gate never enters platform sleep");

    power.recordWakeVerification(firefly::WakeSource::Boot, 100, 100);
    power.recordWakeVerification(firefly::WakeSource::PowerButton, 100, 100);
    power.recordWakeVerification(firefly::WakeSource::RtcAlarm, 100, 100);
    fake_sleep_enter_result = false;
    expect_true(power.attemptLightSleep(fake_sleep_enter) ==
                    firefly::SleepAttemptResult::EnterFailed,
                "failed platform entry is reported");
    expect_true(sleep_prepare_calls == 1 && sleep_restore_calls == 1,
                "failed entry restores prepared resources");

    fake_sleep_enter_result = true;
    expect_true(power.attemptLightSleep(fake_sleep_enter) ==
                    firefly::SleepAttemptResult::Entered,
                "verified platform sleep completes lifecycle");
    expect_true(sleep_prepare_calls == 2 && sleep_restore_calls == 2,
                "successful entry restores prepared resources");
}

class FakeMotionDevice : public firefly::MotionDevice {
public:
    bool begin() override {
        began = true;
        return begin_result;
    }

    bool setLowPower(bool enabled) override {
        low_power = enabled;
        return low_power_result;
    }

    firefly::MotionSample read() override {
        return next_sample;
    }

    firefly::MotionSample next_sample{};
    bool begin_result = true;
    bool low_power_result = true;
    bool began = false;
    bool low_power = false;
};

static void test_motion_service_fixed_sample_buffer() {
    FakeMotionDevice device;
    firefly::MotionService motion(device);
    expect_true(motion.begin() && device.began,
                "motion service begins device");

    device.next_sample.valid = false;
    expect_true(!motion.poll(), "motion service rejects invalid sample");
    expect_true(motion.sampleCount() == 0,
                "invalid motion sample does not enter buffer");

    for(uint32_t i = 0; i < 40; ++i) {
        device.next_sample = {};
        device.next_sample.valid = true;
        device.next_sample.ax = static_cast<float>(i);
        device.next_sample.timestamp_ms = i;
        expect_true(motion.poll(), "motion service accepts valid sample");
    }
    expect_true(motion.sampleCount() == firefly::MotionService::kSampleCapacity,
                "motion sample buffer has fixed capacity");
    expect_true(motion.sampleAt(0).timestamp_ms == 8,
                "motion ring buffer evicts oldest sample");
    expect_true(motion.latest().timestamp_ms == 39,
                "motion ring buffer retains latest sample");
}

static void test_motion_service_low_power_forwarding() {
    FakeMotionDevice device;
    firefly::MotionService motion(device);
    expect_true(motion.setLowPower(true),
                "motion service enters low power mode");
    expect_true(device.low_power,
                "motion service forwards low power request");
}

static void test_motion_power_policy_keeps_gyro_for_screen_off_wrist_raise() {
    expect_true(firefly::MotionPowerPolicy::modeFor(false, false) ==
                    firefly::MotionPowerMode::Normal,
                "active mode keeps normal sampling");
    expect_true(firefly::MotionPowerPolicy::modeFor(true, false) ==
                    firefly::MotionPowerMode::Normal,
                "screen off cpu mode keeps gyro for wrist raise");
    expect_true(firefly::MotionPowerPolicy::modeFor(true, true) ==
                    firefly::MotionPowerMode::LowPower,
                "light sleep preparation uses low power sensor mode");
}

static void test_motion_service_bounded_diagnostics() {
    FakeMotionDevice device;
    firefly::MotionService motion(device);
    firefly::MotionContext context{};

    firefly::MotionSample invalid{};
    expect_true(!motion.processSample(invalid, context),
                "invalid sample is rejected");

    firefly::MotionSample lowered{};
    lowered.valid = true;
    lowered.az = -0.2f;
    lowered.timestamp_ms = 1000;
    motion.processSample(lowered, context);

    firefly::MotionSample raised{};
    raised.valid = true;
    raised.ay = -0.5f;
    raised.az = 0.8f;
    raised.gx = 60.0f;
    raised.timestamp_ms = 1200;
    motion.processSample(raised, context);

    const firefly::MotionDiagnostics diagnostics = motion.diagnostics();
    expect_true(diagnostics.valid_samples == 2,
                "motion diagnostics count valid samples");
    expect_true(diagnostics.invalid_samples == 1,
                "motion diagnostics count invalid samples");
    expect_true(diagnostics.wrist_events == 1,
                "motion diagnostics count wrist events");
    expect_true(diagnostics.steps == motion.summary().steps,
                "motion diagnostics snapshot current step total");
}

static void feed_step_cycle(firefly::StepDetector & detector,
                            uint32_t & timestamp_ms,
                            uint8_t peak_samples,
                            uint8_t baseline_samples) {
    firefly::MotionSample sample{};
    sample.valid = true;
    for(uint8_t i = 0; i < peak_samples; ++i) {
        sample.az = 1.6f;
        sample.timestamp_ms = timestamp_ms;
        detector.update(sample);
        timestamp_ms += 10;
    }
    for(uint8_t i = 0; i < baseline_samples; ++i) {
        sample.az = 1.0f;
        sample.timestamp_ms = timestamp_ms;
        detector.update(sample);
        timestamp_ms += 10;
    }
}

static void test_step_detector_static_and_regular_cadence() {
    firefly::StepDetector detector;
    firefly::MotionSample sample{};
    sample.valid = true;
    sample.az = 1.0f;
    for(uint32_t i = 0; i < 1000; ++i) {
        sample.timestamp_ms = i * 10U;
        detector.update(sample);
    }
    expect_true(detector.totalSteps() == 0,
                "static acceleration produces zero steps");

    uint32_t timestamp_ms = 10000;
    for(uint8_t step = 0; step < 20; ++step) {
        feed_step_cycle(detector, timestamp_ms, 5, 45);
    }
    expect_true(detector.totalSteps() >= 18 && detector.totalSteps() <= 22,
                "regular cadence counts twenty steps within tolerance");
}

static void test_step_detector_limits_high_frequency_jitter() {
    firefly::StepDetector detector;
    uint32_t timestamp_ms = 0;
    for(uint8_t pulse = 0; pulse < 30; ++pulse) {
        feed_step_cycle(detector, timestamp_ms, 2, 8);
    }
    expect_true(detector.totalSteps() <= 12,
                "minimum step interval limits high frequency jitter");
}

static void test_wrist_raise_requires_posture_rotation_and_cooldown() {
    firefly::WristRaiseDetector detector;
    firefly::MotionContext context{};
    firefly::MotionSample lowered{};
    lowered.valid = true;
    lowered.az = -0.2f;
    lowered.timestamp_ms = 1000;
    expect_true(!detector.update(lowered, context),
                "lowered wrist establishes baseline");

    firefly::MotionSample raised{};
    raised.valid = true;
    raised.ay = -0.5f;
    raised.az = 0.8f;
    raised.gx = 60.0f;
    raised.timestamp_ms = 1200;
    expect_true(detector.update(raised, context),
                "posture change plus angular velocity triggers wrist raise");
    raised.timestamp_ms = 2000;
    expect_true(!detector.update(raised, context),
                "wrist raise respects three second cooldown");

    context.screen_on = true;
    lowered.timestamp_ms = 5000;
    detector.update(lowered, context);
    raised.timestamp_ms = 5200;
    expect_true(!detector.update(raised, context),
                "wrist raise is suppressed while screen is on");
}

static void test_motion_service_aggregates_steps_and_wrist_event() {
    FakeMotionDevice device;
    firefly::MotionService motion(device);
    motion.setDayKey(2026183);
    firefly::MotionContext context{};
    firefly::MotionSample sample{};
    sample.valid = true;

    uint32_t timestamp_ms = 0;
    for(uint8_t step = 0; step < 20; ++step) {
        for(uint8_t i = 0; i < 5; ++i) {
            sample.az = 1.6f;
            sample.timestamp_ms = timestamp_ms;
            motion.processSample(sample, context);
            timestamp_ms += 10;
        }
        for(uint8_t i = 0; i < 45; ++i) {
            sample.az = 1.0f;
            sample.timestamp_ms = timestamp_ms;
            motion.processSample(sample, context);
            timestamp_ms += 10;
        }
    }
    const firefly::MotionSummary summary = motion.summary();
    expect_true(summary.steps >= 18 && summary.steps <= 22,
                "motion service aggregates detected steps");
    expect_true(summary.active_minutes == 1,
                "motion service counts unique active minute");

    sample = {};
    sample.valid = true;
    sample.az = -0.2f;
    sample.timestamp_ms = 20000;
    motion.processSample(sample, context);
    sample.ay = -0.5f;
    sample.az = 0.8f;
    sample.gx = 60.0f;
    sample.timestamp_ms = 20200;
    motion.processSample(sample, context);
    expect_true(motion.consumeWristRaise(),
                "motion service publishes wrist raise once");
    expect_true(!motion.consumeWristRaise(),
                "motion service wrist event is consumed");

    motion.setDayKey(2026184);
    expect_true(motion.summary().steps == 0,
                "motion service resets daily aggregate on day change");
}

static void test_motion_service_restores_same_day_aggregate() {
    FakeMotionDevice device;
    firefly::MotionService motion(device);
    motion.restoreDailySummary(2026183, 4321, 27);

    const firefly::MotionSummary restored = motion.summary();
    expect_true(restored.steps == 4321,
                "motion service restores persisted step total");
    expect_true(restored.active_minutes == 27,
                "motion service restores persisted active minutes");

    motion.setDayKey(2026183);
    expect_true(motion.summary().steps == 4321,
                "same day key preserves restored aggregate");
    motion.setDayKey(2026184);
    expect_true(motion.summary().steps == 0 &&
                    motion.summary().active_minutes == 0,
                "new day clears restored aggregate");
}

void setup() {
    Serial.begin(115200);
    delay(200);
    expect_true(FIREFLYOS_VERSION_MAJOR == 0, "version major");
    expect_true(FIREFLYOS_VERSION_MINOR == 1, "version minor");
    expect_true(FIREFLYOS_VERSION_PATCH == 0, "version patch");
    expect_true(FIREFLYOS_BUILD == 100, "version build");
    test_ble_frame_codec_golden_frames();
    test_notification_service_is_bounded_and_local_only();
    test_companion_settings_resolve_independently_and_persist_first();
    test_companion_settings_frame_is_atomic();
    test_companion_local_settings_and_round_trip();
    test_companion_alarm_latest_slot_preserves_other_slot();
    test_music_target_is_explicit_and_local_first();
    test_music_empty_local_transport_and_volume_commit();
    test_companion_weather_cache_boundaries_and_disconnect();
    test_companion_weather_presenter_states();
    test_companion_calendar_is_bounded_and_disable_preserves_local_date();
    test_find_watch_policy_duration_low_battery_and_cancel();
    test_companion_dispatcher_routes_notifications_and_business_frames();
    test_companion_remote_error_decode_and_text();
    test_connectivity_service_bounded_flow();
    test_ble_message_authenticator();
    test_connectivity_pairing_and_security();
    test_event_bus_fifo();
    test_event_bus_full_policy();
    test_event_bus_preserves_full_critical_queue();
    test_state_store_revision();
    test_capability_registry();
    test_sd_paths_stay_inside_managed_root();
    test_sd_removal_requires_two_consecutive_failures();
    test_theme_manifest_validation();
    test_audio_session_priority();
    test_pcm_wav_header_round_trip();
    test_alarm_ringtone_resources();
    test_media_app_fixed_boundaries();
    test_file_scan_page_is_fixed_and_bounded();
    test_app_registry();
    test_lifecycle_and_resource_governor();
    test_app_manager_publishes_requests();
    test_hardware_abstraction();
    test_default_theme_tokens();
    test_navigation_stack();
    test_overlay_priority_policy();
    test_alarm_next_trigger();
    test_alarm_service_publishes_trigger_event_once();
    test_time_service_invalid_rtc();
    test_time_service_reload_set_and_tick();
    test_time_service_network_sync_is_deferred_for_alarm();
    test_countdown_timer_uses_target_time();
    test_countdown_pause_resume_and_one_shot_expiry();
    test_stopwatch_uses_monotonic_time();
    test_stopwatch_survives_page_visibility_changes();
    test_settings_app_command_queue();
    test_settings_commands_preserve_time_and_alarm_payloads();
    test_storage_settings_defaults_and_namespaces();
    test_storage_legacy_migration_preserves_alarm_fields();
    test_calendar_month_boundaries();
    test_calendar_agenda_truncates_to_eight();
    test_calculator_engine_basic_operations();
    test_calculator_engine_limits_and_errors();
    test_calculator_key_input_flow();
    test_tools_command_queue_is_fixed_fifo();
    test_flashlight_session_policy();
    test_flashlight_controller_posts_brightness_commands();
    test_power_state_machine_timing();
    test_power_battery_priority_and_thresholds();
    test_wifi_session_timeout_and_release();
    test_wifi_connection_and_long_session_limits();
    test_wifi_power_policy_rejects_unsafe_sessions();
    test_wifi_provisioning_is_confirmed_and_persisted_after_connect();
    test_wifi_provisioning_rejects_expired_duplicate_and_unconfirmed();
    test_wifi_provisioning_rejects_busy_replay_and_failed_forget();
    test_wifi_provisioning_v2_uses_monotonic_ttl_without_rtc();
    test_wifi_soft_ap_requires_exclusive_idle_radio();
    test_wifi_inactive_error_state_allows_recovery();
    test_weather_phone_payload_cache_and_freshness();
    test_weather_open_meteo_bounds_and_source_switching();
    test_weather_http_reader_enforces_absolute_deadline();
    test_update_manifest_canonical_signature_and_bounds();
    test_update_service_gates_streams_and_finalizes_once();
    test_update_sources_are_managed_and_fail_closed();
    test_update_coordinator_prefers_sd_and_bounds_commands();
    test_boot_validation_is_pending_only_and_bounded();
    test_diagnostics_ring_metrics_and_explicit_export();
    test_factory_reset_defaults_to_keep_sd_and_requires_generation();
    test_hardware_capabilities_degrade_independently();
    test_bulk_transfer_token_path_hash_and_atomic_commit();
    test_bulk_transfer_rejects_bad_hash_and_cleans_timeout();
    test_bulk_transfer_rolls_back_failed_soft_ap_start();
    test_bulk_transfer_preflight_and_explicit_busy_result();
    test_bulk_transfer_shared_lan_fallback_and_link_loss();
    test_debounced_button_short_and_long_press();
    test_debounced_button_rejects_jitter();
    test_light_sleep_requires_verified_wake_matrix();
    test_light_sleep_attempt_restores_on_success_and_failure();
    test_motion_service_fixed_sample_buffer();
    test_motion_service_low_power_forwarding();
    test_motion_power_policy_keeps_gyro_for_screen_off_wrist_raise();
    test_motion_service_bounded_diagnostics();
    test_step_detector_static_and_regular_cadence();
    test_step_detector_limits_high_frequency_jitter();
    test_wrist_raise_requires_posture_rotation_and_cooldown();
    test_motion_service_aggregates_steps_and_wrist_event();
    test_motion_service_restores_same_day_aggregate();
    Serial.printf("FIREFLY_TEST_RESULT failures=%u\n", failures);
}

void loop() {
    delay(1000);
}
