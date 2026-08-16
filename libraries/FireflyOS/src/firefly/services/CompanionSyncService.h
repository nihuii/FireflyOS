#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../protocol/FrameCodec.h"
#include "AlarmService.h"
#include "NotificationService.h"

namespace firefly {

enum class CompanionSettingKind : uint8_t {
    Alarm = 1,
    Brightness = 2,
    Volume = 3,
    Theme = 4,
};

struct VersionedCompanionSetting {
    static constexpr uint16_t kMaxValueBytes = 256;

    uint32_t revision = 0;
    int64_t changed_at = 0;
    uint16_t value_length = 0;
    uint8_t value[kMaxValueBytes]{};
};

struct CompanionSettingsSnapshot {
    static constexpr uint16_t kSchemaVersion = 1;
    static constexpr uint8_t kCapacity = 4;

    // Alarm is one setting kind. Its value contains the slot most recently
    // changed by an explicit operation; applying it must preserve other slots.
    uint16_t schema_version = kSchemaVersion;
    VersionedCompanionSetting settings[kCapacity]{};
    uint8_t valid[kCapacity]{};
};

class CompanionSettingsPersistence {
public:
    virtual ~CompanionSettingsPersistence() = default;
    virtual bool saveSnapshot(
        const CompanionSettingsSnapshot & snapshot) = 0;
};

class CompanionSettingsResolver {
public:
    static bool isNewerRevision(uint32_t candidate, uint32_t current);
    static const VersionedCompanionSetting & pick(
        const VersionedCompanionSetting & left,
        const VersionedCompanionSetting & right);

private:
    static int compareValue(const VersionedCompanionSetting & left,
                            const VersionedCompanionSetting & right);
};

struct CompanionWeather {
    bool valid = false;
    char city[48]{};
    int16_t temperature_tenths_c = 0;
    uint16_t weather_code = 0;
    int16_t high_tenths_c = 0;
    int16_t low_tenths_c = 0;
    int64_t updated_at_epoch = 0;
};

enum class WeatherFreshness : uint8_t {
    Fresh,
    Stale,
    Old,
    Expired,
};

struct CompanionWeatherView {
    char city[48]{};
    char current[24]{};
    char range[48]{};
    char code[24]{};
    char status[64]{};
};

class CompanionWeatherPresenter {
public:
    static CompanionWeatherView build(const CompanionWeather & weather,
                                      WeatherFreshness freshness,
                                      bool connected);
};

struct CompanionCalendarEntry {
    char title[64]{};
    int64_t start_epoch_ms = 0;
    int64_t end_epoch_ms = 0;
    bool all_day = false;
};

struct CompanionCalendar {
    static constexpr uint8_t kCapacity = 8;

    bool enabled = false;
    uint8_t count = 0;
    int64_t updated_at_epoch_ms = 0;
    CompanionCalendarEntry entries[kCapacity]{};
};

struct FindWatchPlan {
    uint32_t duration_ms = 0;
    bool flash_screen = false;
    bool play_audio = false;
};

struct FindWatchState {
    bool active = false;
    bool flash_screen = false;
    bool play_audio = false;
    uint32_t ends_at_ms = 0;
};

class FindDevicePolicy {
public:
    static constexpr uint8_t kExtremelyLowBatteryPercent = 5;
    static constexpr uint32_t kNormalDurationMs = 30000;
    static constexpr uint32_t kLowBatteryDurationMs = 5000;

    static FindWatchPlan watchPlan(int16_t battery_percent);
};

enum class RemoteMediaCommand : uint8_t {
    PlayPause = 1,
    Previous = 2,
    Next = 3,
    Volume = 4,
};

struct CompanionRemoteError {
    protocol::MessageType failed_type = protocol::MessageType::Error;
    protocol::WireErrorCode code = protocol::WireErrorCode::InvalidPayload;
};

class CompanionSyncService {
public:
    static constexpr uint8_t kSettingCount = 4;
    static constexpr int64_t kWeatherStaleSeconds = 3 * 60 * 60;
    static constexpr int64_t kWeatherExpirySeconds = 24 * 60 * 60;

    explicit CompanionSyncService(
        CompanionSettingsPersistence * persistence = nullptr)
        : persistence_(persistence) {}

    bool applyFrame(const protocol::Frame & frame,
                    uint32_t now_ms,
                    int16_t battery_percent);
    bool applySetting(CompanionSettingKind kind,
                      const VersionedCompanionSetting & setting);
    bool recordLocalSetting(CompanionSettingKind kind,
                            const uint8_t * value,
                            uint16_t value_length,
                            int64_t changed_at);
    bool setting(CompanionSettingKind kind,
                 VersionedCompanionSetting & output) const;
    CompanionSettingsSnapshot settingsSnapshot() const {
        return settings_snapshot_;
    }
    bool restoreSnapshot(const CompanionSettingsSnapshot & snapshot);
    WeatherFreshness weatherAt(int64_t now_epoch,
                               bool connected,
                               CompanionWeather & output) const;
    const CompanionCalendar & calendar() const { return calendar_; }
    FindWatchState findWatchAt(uint32_t now_ms);
    bool cancelFindWatch();

    static bool buildMediaCommand(RemoteMediaCommand command,
                                  uint8_t volume_percent,
                                  uint16_t sequence,
                                  protocol::Frame & output);
    static bool buildFindPhone(uint16_t sequence, protocol::Frame & output);
    // SettingsGet carries one schema byte. The watch answers with one bounded,
    // full SettingsSet snapshot; the phone resolves each of the four kinds and
    // pushes the winning full snapshot back.
    static bool buildSettingsSnapshot(
        const CompanionSettingsSnapshot & snapshot,
        uint16_t sequence,
        protocol::Frame & output);
    static bool decodeAlarm(const VersionedCompanionSetting & setting,
                            uint8_t & slot,
                            Alarm & alarm);
    static bool decodeError(const protocol::Frame & frame,
                            CompanionRemoteError & output);
    static const char * remoteErrorText(
        const CompanionRemoteError & error);

private:
    static int8_t settingIndex(CompanionSettingKind kind);
    static bool validSetting(CompanionSettingKind kind,
                             const VersionedCompanionSetting & setting);
    static bool validSnapshot(const CompanionSettingsSnapshot & snapshot);
    bool persistAndCommit(const CompanionSettingsSnapshot & staged);
    bool applySettingsFrame(const protocol::Frame & frame);
    bool applyWeatherFrame(const protocol::Frame & frame);
    bool applyCalendarFrame(const protocol::Frame & frame);
    bool applyFindWatchFrame(const protocol::Frame & frame,
                             uint32_t now_ms,
                             int16_t battery_percent);

    CompanionSettingsPersistence * persistence_ = nullptr;
    CompanionSettingsSnapshot settings_snapshot_{};
    CompanionWeather weather_{};
    CompanionCalendar calendar_{};
    FindWatchState find_watch_{};
};

enum class CompanionDispatchResult : uint8_t {
    Invalid,
    Notification,
    Companion,
};

class CompanionFrameDispatcher {
public:
    CompanionFrameDispatcher(NotificationService & notifications,
                             CompanionSyncService & companion)
        : notifications_(notifications), companion_(companion) {}

    CompanionDispatchResult dispatch(const protocol::Frame & frame,
                                     uint32_t now_ms,
                                     int16_t battery_percent);

private:
    NotificationService & notifications_;
    CompanionSyncService & companion_;
};

static_assert(sizeof(CompanionWeather) <= 80,
              "weather cache must remain fixed and bounded");
static_assert(sizeof(CompanionWeatherView) <= 216,
              "weather presentation must remain fixed and bounded");
static_assert(sizeof(CompanionCalendarEntry) <= 88,
              "calendar entry must remain fixed and bounded");

}  // namespace firefly
