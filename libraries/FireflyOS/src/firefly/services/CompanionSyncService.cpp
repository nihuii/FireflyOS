#include "CompanionSyncService.h"

#include <stdio.h>
#include <string.h>

namespace firefly {
namespace {

constexpr uint8_t kSchema = 1;
constexpr uint16_t kSettingsHeaderBytes = 2;
constexpr uint16_t kSettingHeaderBytes = 15;
constexpr uint16_t kWeatherFixedBytes = 18;
constexpr uint16_t kCalendarHeaderBytes = 11;
constexpr uint16_t kCalendarEntryFixedBytes = 18;

uint16_t readU16(const uint8_t * input) {
    return static_cast<uint16_t>(
        input[0] | (static_cast<uint16_t>(input[1]) << 8)
    );
}

int16_t readI16(const uint8_t * input) {
    return static_cast<int16_t>(readU16(input));
}

uint32_t readU32(const uint8_t * input) {
    return static_cast<uint32_t>(input[0]) |
        (static_cast<uint32_t>(input[1]) << 8) |
        (static_cast<uint32_t>(input[2]) << 16) |
        (static_cast<uint32_t>(input[3]) << 24);
}

int64_t readI64(const uint8_t * input) {
    uint64_t value = 0;
    for(uint8_t index = 0; index < 8; ++index) {
        value |= static_cast<uint64_t>(input[index]) << (index * 8);
    }
    return static_cast<int64_t>(value);
}

void writeU16(uint8_t * output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value & 0xFF);
    output[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void writeU32(uint8_t * output, uint32_t value) {
    for(uint8_t index = 0; index < 4; ++index) {
        output[index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

void writeI64(uint8_t * output, int64_t value) {
    const uint64_t raw = static_cast<uint64_t>(value);
    for(uint8_t index = 0; index < 8; ++index) {
        output[index] = static_cast<uint8_t>(raw >> (index * 8));
    }
}

bool validUtf8(const uint8_t * input, uint16_t length) {
    if(!input && length > 0) return false;
    uint16_t index = 0;
    while(index < length) {
        const uint8_t first = input[index];
        uint8_t extra = 0;
        uint32_t codepoint = 0;
        if(first >= 0x01 && first <= 0x7F) {
            codepoint = first;
        } else if(first >= 0xC2 && first <= 0xDF) {
            extra = 1;
            codepoint = first & 0x1F;
        } else if(first >= 0xE0 && first <= 0xEF) {
            extra = 2;
            codepoint = first & 0x0F;
        } else if(first >= 0xF0 && first <= 0xF4) {
            extra = 3;
            codepoint = first & 0x07;
        } else {
            return false;
        }
        if(static_cast<uint16_t>(index + extra) >= length) return false;
        for(uint8_t offset = 1; offset <= extra; ++offset) {
            const uint8_t continuation = input[index + offset];
            if((continuation & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }
        if((extra == 2 && codepoint < 0x800) ||
           (extra == 3 && codepoint < 0x10000) ||
           (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
           codepoint > 0x10FFFF) {
            return false;
        }
        index = static_cast<uint16_t>(index + extra + 1);
    }
    return true;
}

bool copyUtf8(char * output,
              size_t capacity,
              const uint8_t * input,
              uint16_t length,
              bool allow_empty = false) {
    if(!output || capacity == 0 || length >= capacity ||
       (!allow_empty && length == 0) || !validUtf8(input, length)) {
        return false;
    }
    if(length > 0) memcpy(output, input, length);
    output[length] = '\0';
    return true;
}

bool dueOrPast(uint32_t now_ms, uint32_t deadline_ms) {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

void formatTenths(char * output,
                  size_t capacity,
                  const char * prefix,
                  int16_t value) {
    const int32_t signed_value = value;
    const bool negative = signed_value < 0;
    const uint32_t magnitude = static_cast<uint32_t>(
        negative ? -signed_value : signed_value
    );
    snprintf(output, capacity, "%s%s%lu.%lu C",
             prefix,
             negative ? "-" : "",
             static_cast<unsigned long>(magnitude / 10),
             static_cast<unsigned long>(magnitude % 10));
}

}  // namespace

bool CompanionSettingsResolver::isNewerRevision(uint32_t candidate,
                                                uint32_t current) {
    const uint32_t delta = candidate - current;
    return delta != 0 && delta < 0x80000000UL;
}

const VersionedCompanionSetting & CompanionSettingsResolver::pick(
    const VersionedCompanionSetting & left,
    const VersionedCompanionSetting & right) {
    if(left.changed_at != right.changed_at) {
        return left.changed_at > right.changed_at ? left : right;
    }
    if(isNewerRevision(left.revision, right.revision)) return left;
    if(isNewerRevision(right.revision, left.revision)) return right;
    return compareValue(left, right) >= 0 ? left : right;
}

int CompanionSettingsResolver::compareValue(
    const VersionedCompanionSetting & left,
    const VersionedCompanionSetting & right) {
    const uint16_t shared = left.value_length < right.value_length
        ? left.value_length : right.value_length;
    if(shared > 0) {
        const int comparison = memcmp(left.value, right.value, shared);
        if(comparison != 0) return comparison;
    }
    if(left.value_length == right.value_length) return 0;
    return left.value_length > right.value_length ? 1 : -1;
}

FindWatchPlan FindDevicePolicy::watchPlan(int16_t battery_percent) {
    FindWatchPlan plan{};
    plan.flash_screen = true;
    if(battery_percent >= 0 &&
       battery_percent <= kExtremelyLowBatteryPercent) {
        plan.duration_ms = kLowBatteryDurationMs;
        plan.play_audio = false;
    } else {
        plan.duration_ms = kNormalDurationMs;
        plan.play_audio = true;
    }
    return plan;
}

CompanionWeatherView CompanionWeatherPresenter::build(
    const CompanionWeather & weather,
    WeatherFreshness freshness,
    bool connected) {
    CompanionWeatherView view{};
    if(freshness == WeatherFreshness::Expired || !weather.valid) {
        strlcpy(view.city, "Weather", sizeof(view.city));
        strlcpy(view.current, "--.- C", sizeof(view.current));
        strlcpy(view.range, "High --.- | Low --.-", sizeof(view.range));
        strlcpy(view.code, "Code --", sizeof(view.code));
        snprintf(view.status, sizeof(view.status),
                 "unavailable | expired (>24h)%s",
                 connected ? "" : " | offline");
        return view;
    }

    strlcpy(view.city, weather.city, sizeof(view.city));
    formatTenths(view.current, sizeof(view.current), "Current ",
                 weather.temperature_tenths_c);
    char high[16]{};
    char low[16]{};
    formatTenths(high, sizeof(high), "", weather.high_tenths_c);
    formatTenths(low, sizeof(low), "", weather.low_tenths_c);
    snprintf(view.range, sizeof(view.range), "High %s | Low %s", high, low);
    snprintf(view.code, sizeof(view.code), "Code %u",
             static_cast<unsigned>(weather.weather_code));
    snprintf(view.status, sizeof(view.status), "%s%s",
             freshness == WeatherFreshness::Fresh
                 ? "fresh" : "stale (>3h)",
             connected ? "" : " | offline");
    return view;
}

bool CompanionSyncService::applyFrame(const protocol::Frame & frame,
                                      uint32_t now_ms,
                                      int16_t battery_percent) {
    if(frame.payload_length > protocol::kMaxPayload) return false;
    switch(frame.type) {
        case protocol::MessageType::SettingsGet:
            return frame.payload_length == 1 && frame.payload[0] == kSchema;
        case protocol::MessageType::SettingsSet:
            return applySettingsFrame(frame);
        case protocol::MessageType::WeatherUpdate:
            return applyWeatherFrame(frame);
        case protocol::MessageType::CalendarUpdate:
            return applyCalendarFrame(frame);
        case protocol::MessageType::FindWatch:
            return applyFindWatchFrame(frame, now_ms, battery_percent);
        default:
            return false;
    }
}

bool CompanionSyncService::recordLocalSetting(
    CompanionSettingKind kind,
    const uint8_t * value,
    uint16_t value_length,
    int64_t changed_at) {
    const int8_t index = settingIndex(kind);
    if(index < 0 || value == nullptr || value_length == 0 ||
       value_length > VersionedCompanionSetting::kMaxValueBytes) {
        return false;
    }
    CompanionSettingsSnapshot staged = settings_snapshot_;
    VersionedCompanionSetting next{};
    int64_t logical_changed_at = changed_at;
    for(uint8_t setting_index = 0;
        setting_index < kSettingCount;
        ++setting_index) {
        if(staged.valid[setting_index] &&
           staged.settings[setting_index].changed_at > logical_changed_at) {
            logical_changed_at =
                staged.settings[setting_index].changed_at;
        }
    }
    if(staged.valid[index]) {
        next.revision = staged.settings[index].revision + 1U;
    } else {
        next.revision = 1;
    }
    next.changed_at = logical_changed_at;
    next.value_length = value_length;
    memcpy(next.value, value, value_length);
    if(!validSetting(kind, next)) return false;
    staged.settings[index] = next;
    staged.valid[index] = 1;
    return persistAndCommit(staged);
}

bool CompanionSyncService::applySetting(
    CompanionSettingKind kind,
    const VersionedCompanionSetting & setting) {
    const int8_t index = settingIndex(kind);
    if(index < 0 || !validSetting(kind, setting)) return false;
    CompanionSettingsSnapshot staged = settings_snapshot_;
    if(staged.valid[index]) {
        const VersionedCompanionSetting & winner =
            CompanionSettingsResolver::pick(staged.settings[index], setting);
        if(&winner != &setting) return false;
    }
    staged.settings[index] = setting;
    staged.valid[index] = 1;
    return persistAndCommit(staged);
}

bool CompanionSyncService::setting(
    CompanionSettingKind kind,
    VersionedCompanionSetting & output) const {
    const int8_t index = settingIndex(kind);
    if(index < 0 || !settings_snapshot_.valid[index]) return false;
    output = settings_snapshot_.settings[index];
    return true;
}

bool CompanionSyncService::restoreSnapshot(
    const CompanionSettingsSnapshot & snapshot) {
    if(!validSnapshot(snapshot)) return false;
    settings_snapshot_ = snapshot;
    return true;
}

WeatherFreshness CompanionSyncService::weatherAt(
    int64_t now_epoch,
    bool connected,
    CompanionWeather & output) const {
    (void)connected;
    output = weather_;
    if(!weather_.valid) return WeatherFreshness::Expired;
    int64_t age = now_epoch - weather_.updated_at_epoch;
    if(age < 0) age = 0;
    if(age > kWeatherExpirySeconds) {
        output = CompanionWeather{};
        return WeatherFreshness::Expired;
    }
    return age > kWeatherStaleSeconds
        ? WeatherFreshness::Stale : WeatherFreshness::Fresh;
}

FindWatchState CompanionSyncService::findWatchAt(uint32_t now_ms) {
    if(find_watch_.active && dueOrPast(now_ms, find_watch_.ends_at_ms)) {
        cancelFindWatch();
    }
    return find_watch_;
}

bool CompanionSyncService::cancelFindWatch() {
    if(!find_watch_.active) return false;
    find_watch_ = FindWatchState{};
    return true;
}

bool CompanionSyncService::buildMediaCommand(
    RemoteMediaCommand command,
    uint8_t volume_percent,
    uint16_t sequence,
    protocol::Frame & output) {
    if(command == RemoteMediaCommand::Volume && volume_percent > 100) {
        return false;
    }
    output = protocol::Frame{};
    output.type = protocol::MessageType::MediaCommand;
    output.flags = protocol::FrameFlag::AckRequired;
    output.sequence = sequence;
    output.payload[0] = static_cast<uint8_t>(command);
    output.payload_length = command == RemoteMediaCommand::Volume ? 2 : 1;
    if(output.payload_length == 2) output.payload[1] = volume_percent;
    return true;
}

bool CompanionSyncService::buildFindPhone(uint16_t sequence,
                                          protocol::Frame & output) {
    output = protocol::Frame{};
    output.type = protocol::MessageType::FindPhone;
    output.flags = protocol::FrameFlag::AckRequired;
    output.sequence = sequence;
    output.payload[0] = kSchema;
    output.payload[1] = 1;
    output.payload_length = 2;
    return true;
}

bool CompanionSyncService::buildSettingsSnapshot(
    const CompanionSettingsSnapshot & snapshot,
    uint16_t sequence,
    protocol::Frame & output) {
    if(!validSnapshot(snapshot)) return false;
    output = protocol::Frame{};
    output.type = protocol::MessageType::SettingsSet;
    output.flags = protocol::FrameFlag::AckRequired;
    output.sequence = sequence;
    output.payload[0] = kSchema;
    output.payload[1] = 0;
    uint16_t offset = kSettingsHeaderBytes;
    for(uint8_t index = 0; index < kSettingCount; ++index) {
        if(!snapshot.valid[index]) continue;
        const VersionedCompanionSetting & setting =
            snapshot.settings[index];
        const uint32_t required = static_cast<uint32_t>(offset) +
            kSettingHeaderBytes + setting.value_length;
        if(required > protocol::kMaxPayload) return false;
        output.payload[offset] = static_cast<uint8_t>(index + 1);
        writeU32(output.payload + offset + 1, setting.revision);
        writeI64(output.payload + offset + 5, setting.changed_at);
        writeU16(output.payload + offset + 13, setting.value_length);
        offset = static_cast<uint16_t>(offset + kSettingHeaderBytes);
        memcpy(output.payload + offset, setting.value, setting.value_length);
        offset = static_cast<uint16_t>(offset + setting.value_length);
        ++output.payload[1];
    }
    output.payload_length = offset;
    return true;
}

bool CompanionSyncService::decodeAlarm(
    const VersionedCompanionSetting & setting,
    uint8_t & slot,
    Alarm & alarm) {
    if(!validSetting(CompanionSettingKind::Alarm, setting)) return false;
    slot = setting.value[0];
    alarm = Alarm{};
    alarm.configured = setting.value[1] == 1;
    alarm.enabled = setting.value[2] == 1;
    alarm.hour = setting.value[3];
    alarm.minute = setting.value[4];
    alarm.days_mask = setting.value[5];
    alarm.ringtone = setting.value[6];
    const uint8_t name_length = setting.value[7];
    if(name_length > 0) {
        memcpy(alarm.name, setting.value + 8, name_length);
    }
    alarm.name[name_length] = '\0';
    return true;
}

bool CompanionSyncService::decodeError(
    const protocol::Frame & frame,
    CompanionRemoteError & output) {
    if(frame.type != protocol::MessageType::Error ||
       frame.payload_length != 3 ||
       frame.payload[0] != kSchema ||
       !protocol::isKnownMessageType(frame.payload[1]) ||
       frame.payload[2] <
           static_cast<uint8_t>(protocol::WireErrorCode::InvalidPayload) ||
       frame.payload[2] >
           static_cast<uint8_t>(protocol::WireErrorCode::Unauthorized)) {
        return false;
    }
    output.failed_type =
        static_cast<protocol::MessageType>(frame.payload[1]);
    output.code =
        static_cast<protocol::WireErrorCode>(frame.payload[2]);
    return true;
}

const char * CompanionSyncService::remoteErrorText(
    const CompanionRemoteError & error) {
    switch(error.code) {
        case protocol::WireErrorCode::NoActiveMediaSession:
            return "NO MEDIA";
        case protocol::WireErrorCode::MediaAccessRequired:
            return "MEDIA ACCESS";
        case protocol::WireErrorCode::SecurityDenied:
            return "PHONE DENIED";
        case protocol::WireErrorCode::FindPhoneUnavailable:
            return "PHONE N/A";
        case protocol::WireErrorCode::PersistenceFailure:
            return "SAVE FAILED";
        case protocol::WireErrorCode::Unauthorized:
            return "UNAUTHORIZED";
        case protocol::WireErrorCode::InvalidPayload:
        default:
            return "PHONE ERROR";
    }
}

int8_t CompanionSyncService::settingIndex(CompanionSettingKind kind) {
    const uint8_t value = static_cast<uint8_t>(kind);
    return value >= 1 && value <= kSettingCount
        ? static_cast<int8_t>(value - 1) : -1;
}

bool CompanionSyncService::validSetting(
    CompanionSettingKind kind,
    const VersionedCompanionSetting & setting) {
    if(setting.value_length == 0 ||
       setting.value_length > VersionedCompanionSetting::kMaxValueBytes) {
        return false;
    }
    switch(kind) {
        case CompanionSettingKind::Brightness:
            return setting.value_length == 1 && setting.value[0] >= 20;
        case CompanionSettingKind::Volume:
            return setting.value_length == 1 && setting.value[0] <= 100;
        case CompanionSettingKind::Theme:
            return setting.value_length < 24 &&
                validUtf8(setting.value, setting.value_length);
        case CompanionSettingKind::Alarm: {
            if(setting.value_length < 8 || setting.value_length > 64) {
                return false;
            }
            const uint8_t name_length = setting.value[7];
            return setting.value[0] < AlarmService::kSlots &&
                setting.value[1] <= 1 && setting.value[2] <= 1 &&
                setting.value[3] <= 23 && setting.value[4] <= 59 &&
                setting.value[5] != 0 && (setting.value[5] & 0x80) == 0 &&
                setting.value[6] < AlarmService::kRingtoneCount &&
                name_length < sizeof(Alarm::name) &&
                static_cast<uint16_t>(8 + name_length) ==
                    setting.value_length &&
                validUtf8(setting.value + 8, name_length);
        }
        default:
            return false;
    }
}

bool CompanionSyncService::validSnapshot(
    const CompanionSettingsSnapshot & snapshot) {
    if(snapshot.schema_version != CompanionSettingsSnapshot::kSchemaVersion) {
        return false;
    }
    for(uint8_t index = 0; index < kSettingCount; ++index) {
        if(snapshot.valid[index] > 1) return false;
        if(snapshot.valid[index] &&
           !validSetting(static_cast<CompanionSettingKind>(index + 1),
                         snapshot.settings[index])) {
            return false;
        }
    }
    return true;
}

bool CompanionSyncService::persistAndCommit(
    const CompanionSettingsSnapshot & staged) {
    if(!validSnapshot(staged)) return false;
    if(persistence_ && !persistence_->saveSnapshot(staged)) return false;
    settings_snapshot_ = staged;
    return true;
}

bool CompanionSyncService::applySettingsFrame(const protocol::Frame & frame) {
    if(frame.payload_length < kSettingsHeaderBytes ||
       frame.payload[0] != kSchema ||
       frame.payload[1] > kSettingCount) {
        return false;
    }
    bool seen[kSettingCount]{};
    bool candidate_valid[kSettingCount]{};
    VersionedCompanionSetting candidates[kSettingCount]{};
    uint16_t offset = kSettingsHeaderBytes;
    for(uint8_t entry = 0; entry < frame.payload[1]; ++entry) {
        if(static_cast<uint32_t>(offset) + kSettingHeaderBytes >
           frame.payload_length) {
            return false;
        }
        const CompanionSettingKind kind =
            static_cast<CompanionSettingKind>(frame.payload[offset]);
        const int8_t index = settingIndex(kind);
        if(index < 0 || seen[index]) return false;
        seen[index] = true;

        VersionedCompanionSetting setting{};
        setting.revision = readU32(frame.payload + offset + 1);
        setting.changed_at = readI64(frame.payload + offset + 5);
        setting.value_length = readU16(frame.payload + offset + 13);
        offset = static_cast<uint16_t>(offset + kSettingHeaderBytes);
        if(setting.value_length > VersionedCompanionSetting::kMaxValueBytes ||
           static_cast<uint32_t>(offset) + setting.value_length >
               frame.payload_length) {
            return false;
        }
        if(setting.value_length > 0) {
            memcpy(setting.value, frame.payload + offset, setting.value_length);
        }
        offset = static_cast<uint16_t>(offset + setting.value_length);
        if(!validSetting(kind, setting)) return false;
        candidates[index] = setting;
        candidate_valid[index] = true;
    }
    if(offset != frame.payload_length) return false;

    CompanionSettingsSnapshot staged = settings_snapshot_;
    bool changed = false;
    for(uint8_t index = 0; index < kSettingCount; ++index) {
        if(!candidate_valid[index]) continue;
        if(staged.valid[index] &&
           &CompanionSettingsResolver::pick(staged.settings[index],
                                             candidates[index]) !=
               &candidates[index]) {
            continue;
        }
        staged.settings[index] = candidates[index];
        staged.valid[index] = 1;
        changed = true;
    }
    return !changed || persistAndCommit(staged);
}

bool CompanionSyncService::applyWeatherFrame(const protocol::Frame & frame) {
    if(frame.payload_length <= kWeatherFixedBytes ||
       frame.payload[0] != kSchema) {
        return false;
    }
    const uint8_t city_length = frame.payload[1];
    if(city_length == 0 || city_length >= sizeof(weather_.city) ||
       static_cast<uint16_t>(kWeatherFixedBytes + city_length) !=
           frame.payload_length) {
        return false;
    }
    CompanionWeather decoded{};
    if(!copyUtf8(decoded.city, sizeof(decoded.city),
                 frame.payload + kWeatherFixedBytes, city_length)) {
        return false;
    }
    decoded.weather_code = readU16(frame.payload + 2);
    decoded.temperature_tenths_c = readI16(frame.payload + 4);
    decoded.high_tenths_c = readI16(frame.payload + 6);
    decoded.low_tenths_c = readI16(frame.payload + 8);
    decoded.updated_at_epoch = readI64(frame.payload + 10);
    decoded.valid = true;
    weather_ = decoded;
    return true;
}

bool CompanionSyncService::applyCalendarFrame(const protocol::Frame & frame) {
    if(frame.payload_length < kCalendarHeaderBytes ||
       frame.payload[0] != kSchema ||
       frame.payload[1] > 1 ||
       frame.payload[2] > CompanionCalendar::kCapacity ||
       (frame.payload[1] == 0 && frame.payload[2] != 0)) {
        return false;
    }
    CompanionCalendar decoded{};
    decoded.enabled = frame.payload[1] == 1;
    decoded.count = frame.payload[2];
    decoded.updated_at_epoch_ms = readI64(frame.payload + 3);
    uint16_t offset = kCalendarHeaderBytes;
    for(uint8_t index = 0; index < decoded.count; ++index) {
        if(static_cast<uint32_t>(offset) + kCalendarEntryFixedBytes >
           frame.payload_length) {
            return false;
        }
        const uint8_t title_length = frame.payload[offset];
        CompanionCalendarEntry & entry = decoded.entries[index];
        entry.start_epoch_ms = readI64(frame.payload + offset + 1);
        entry.end_epoch_ms = readI64(frame.payload + offset + 9);
        const uint8_t all_day = frame.payload[offset + 17];
        offset = static_cast<uint16_t>(offset + kCalendarEntryFixedBytes);
        if(title_length >= sizeof(entry.title) || all_day > 1 ||
           entry.end_epoch_ms < entry.start_epoch_ms ||
           static_cast<uint32_t>(offset) + title_length >
               frame.payload_length ||
           !copyUtf8(entry.title, sizeof(entry.title), frame.payload + offset,
                     title_length, true)) {
            return false;
        }
        entry.all_day = all_day == 1;
        offset = static_cast<uint16_t>(offset + title_length);
    }
    if(offset != frame.payload_length) return false;
    calendar_ = decoded;
    return true;
}

bool CompanionSyncService::applyFindWatchFrame(
    const protocol::Frame & frame,
    uint32_t now_ms,
    int16_t battery_percent) {
    if(frame.payload_length != 2 || frame.payload[0] != kSchema ||
       frame.payload[1] > 1) {
        return false;
    }
    if(frame.payload[1] == 0) {
        cancelFindWatch();
        return true;
    }
    const FindWatchPlan plan = FindDevicePolicy::watchPlan(battery_percent);
    find_watch_.active = true;
    find_watch_.flash_screen = plan.flash_screen;
    find_watch_.play_audio = plan.play_audio;
    find_watch_.ends_at_ms = now_ms + plan.duration_ms;
    return true;
}

CompanionDispatchResult CompanionFrameDispatcher::dispatch(
    const protocol::Frame & frame,
    uint32_t now_ms,
    int16_t battery_percent) {
    if(frame.type == protocol::MessageType::NotificationPush ||
       frame.type == protocol::MessageType::NotificationDismiss) {
        return notifications_.applyFrame(frame)
            ? CompanionDispatchResult::Notification
            : CompanionDispatchResult::Invalid;
    }
    return companion_.applyFrame(frame, now_ms, battery_percent)
        ? CompanionDispatchResult::Companion
        : CompanionDispatchResult::Invalid;
}

}  // namespace firefly
