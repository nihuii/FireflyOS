#include "StorageService.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_littlefs.h>
#include <string.h>

namespace firefly {
namespace {

bool openNamespace(Preferences & preferences,
                   const char * name,
                   bool read_only) {
    return preferences.begin(name, read_only);
}

void alarmKey(char * out, size_t out_size, uint8_t slot,
              const char * suffix) {
    snprintf(out, out_size, "a%u_%s", static_cast<unsigned>(slot), suffix);
}

void legacyAlarmKey(char * out, size_t out_size, uint8_t slot,
                    const char * suffix) {
    snprintf(out, out_size, "al%u_%s", static_cast<unsigned>(slot), suffix);
}

bool writeSchema(Preferences & preferences) {
    return preferences.putUShort("schema", StorageService::kSchemaVersion) ==
           sizeof(uint16_t);
}

}  // namespace

bool StorageService::begin() {
    const bool namespaces_ready = initializeNamespaces();
    const bool migration_ready = namespaces_ready && migrateLegacyPreferences();
    mountLittleFs();
    return namespaces_ready && migration_ready;
}

bool StorageService::initializeNamespaces() {
    const char * namespaces[] = {
        kSystemNamespace, kAlarmNamespace, kPairNamespace, kStatsNamespace
    };
    for(const char * name : namespaces) {
        Preferences preferences;
        if(!openNamespace(preferences, name, false)) {
            recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
            return false;
        }
        const uint16_t schema = preferences.getUShort("schema", 0);
        if(schema == 0 && !writeSchema(preferences)) {
            preferences.end();
            recordFailure(StorageDiagnosticCode::WriteFailed);
            return false;
        }
        if(schema != 0 && schema != kSchemaVersion) {
            preferences.end();
            recordFailure(StorageDiagnosticCode::SchemaMismatch);
            return false;
        }
        preferences.end();
    }
    return true;
}

bool StorageService::loadSettings(SystemSettings & settings) {
    settings = SystemSettings{};
    Preferences preferences;
    if(!openNamespace(preferences, kSystemNamespace, true)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    if(preferences.getUShort("schema", 0) != kSchemaVersion) {
        preferences.end();
        recordFailure(StorageDiagnosticCode::SchemaMismatch);
        return false;
    }
    settings.volume = preferences.getUChar("volume", settings.volume);
    settings.brightness = preferences.getUChar("bright", settings.brightness);
    settings.auto_sleep_seconds =
        preferences.getUShort("sleep_s", settings.auto_sleep_seconds);
    settings.hide_notification_content =
        preferences.getBool("hide_notif", settings.hide_notification_content);
    settings.wrist_raise_enabled =
        preferences.getBool("wrist", settings.wrist_raise_enabled);
    const String theme = preferences.getString("theme", settings.theme_id);
    strlcpy(settings.theme_id, theme.c_str(), sizeof(settings.theme_id));
    preferences.end();

    if(settings.volume > 100) settings.volume = 100;
    if(settings.auto_sleep_seconds > 3600) settings.auto_sleep_seconds = 30;
    return true;
}

bool StorageService::saveSettings(const SystemSettings & settings) {
    Preferences preferences;
    if(!openNamespace(preferences, kSystemNamespace, false)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    const bool ok = writeSchema(preferences) &&
        preferences.putUChar("volume", settings.volume > 100
            ? 100 : settings.volume) == sizeof(uint8_t) &&
        preferences.putUChar("bright", settings.brightness) == sizeof(uint8_t) &&
        preferences.putUShort("sleep_s", settings.auto_sleep_seconds) ==
            sizeof(uint16_t) &&
        preferences.putBool("hide_notif", settings.hide_notification_content) ==
            sizeof(bool) &&
        preferences.putBool("wrist", settings.wrist_raise_enabled) == sizeof(bool) &&
        preferences.putString("theme", settings.theme_id) > 0;
    preferences.end();
    if(!ok) recordFailure(StorageDiagnosticCode::WriteFailed);
    return ok;
}

bool StorageService::loadAlarm(uint8_t slot, Alarm & alarm, bool & present) {
    alarm = Alarm{};
    present = false;
    if(slot >= AlarmService::kSlots) return false;
    Preferences preferences;
    if(!openNamespace(preferences, kAlarmNamespace, true)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    if(preferences.getUShort("schema", 0) != kSchemaVersion) {
        preferences.end();
        recordFailure(StorageDiagnosticCode::SchemaMismatch);
        return false;
    }
    char key[16];
    alarmKey(key, sizeof(key), slot, "cfg");
    present = preferences.isKey(key);
    if(present) {
        alarm.configured = preferences.getBool(key, false);
        alarmKey(key, sizeof(key), slot, "en");
        alarm.enabled = preferences.getBool(key, false);
        alarmKey(key, sizeof(key), slot, "hr");
        alarm.hour = preferences.getUChar(key, 7);
        alarmKey(key, sizeof(key), slot, "mn");
        alarm.minute = preferences.getUChar(key, 30);
        alarmKey(key, sizeof(key), slot, "dy");
        alarm.days_mask = preferences.getUChar(key, 0x7F);
        alarmKey(key, sizeof(key), slot, "rg");
        alarm.ringtone = preferences.getUChar(key, 0);
        alarmKey(key, sizeof(key), slot, "nm");
        const String name = preferences.getString(key, alarm.name);
        strlcpy(alarm.name, name.c_str(), sizeof(alarm.name));
    }
    preferences.end();
    if(alarm.hour > 23 || alarm.minute > 59 || alarm.days_mask == 0) {
        alarm = Alarm{};
        present = false;
        recordFailure(StorageDiagnosticCode::ReadFailed);
        return false;
    }
    return true;
}

bool StorageService::saveAlarm(uint8_t slot, const Alarm & alarm) {
    if(slot >= AlarmService::kSlots || alarm.hour > 23 ||
       alarm.minute > 59 || alarm.days_mask == 0) return false;
    Preferences preferences;
    if(!openNamespace(preferences, kAlarmNamespace, false)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    char key[16];
    alarmKey(key, sizeof(key), slot, "cfg");
    bool ok = writeSchema(preferences) &&
              preferences.putBool(key, alarm.configured) == sizeof(bool);
    alarmKey(key, sizeof(key), slot, "en");
    ok = ok && preferences.putBool(key, alarm.enabled) == sizeof(bool);
    alarmKey(key, sizeof(key), slot, "hr");
    ok = ok && preferences.putUChar(key, alarm.hour) == sizeof(uint8_t);
    alarmKey(key, sizeof(key), slot, "mn");
    ok = ok && preferences.putUChar(key, alarm.minute) == sizeof(uint8_t);
    alarmKey(key, sizeof(key), slot, "dy");
    ok = ok && preferences.putUChar(key, alarm.days_mask) == sizeof(uint8_t);
    alarmKey(key, sizeof(key), slot, "rg");
    ok = ok && preferences.putUChar(key, alarm.ringtone) == sizeof(uint8_t);
    alarmKey(key, sizeof(key), slot, "nm");
    ok = ok && preferences.putString(key, alarm.name) > 0;
    preferences.end();
    if(!ok) recordFailure(StorageDiagnosticCode::WriteFailed);
    return ok;
}

bool StorageService::loadActivityStats(ActivityStats & stats) {
    stats = ActivityStats{};
    Preferences preferences;
    if(!openNamespace(preferences, kStatsNamespace, true)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    if(preferences.getUShort("schema", 0) != kSchemaVersion) {
        preferences.end();
        recordFailure(StorageDiagnosticCode::SchemaMismatch);
        return false;
    }
    stats.day_key = preferences.getUInt("day", 0);
    stats.steps = preferences.getUInt("steps", 0);
    stats.active_minutes = preferences.getUShort("active", 0);
    preferences.end();
    return true;
}

bool StorageService::saveActivityStats(const ActivityStats & stats) {
    Preferences preferences;
    if(!openNamespace(preferences, kStatsNamespace, false)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    const bool ok = writeSchema(preferences) &&
        preferences.putUInt("day", stats.day_key) == sizeof(uint32_t) &&
        preferences.putUInt("steps", stats.steps) == sizeof(uint32_t) &&
        preferences.putUShort("active", stats.active_minutes) == sizeof(uint16_t);
    preferences.end();
    if(!ok) recordFailure(StorageDiagnosticCode::WriteFailed);
    return ok;
}

bool StorageService::loadThemeTokens(void * data, size_t length) {
    if(!data || length == 0 || length > 256) return false;
    Preferences preferences;
    if(!openNamespace(preferences, kSystemNamespace, true)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    const bool present = preferences.getBytesLength("theme_v1") == length;
    const bool ok = present &&
        preferences.getBytes("theme_v1", data, length) == length;
    preferences.end();
    if(present && !ok) recordFailure(StorageDiagnosticCode::ReadFailed);
    return ok;
}

bool StorageService::saveThemeTokens(const void * data, size_t length) {
    if(!data || length == 0 || length > 256) return false;
    Preferences preferences;
    if(!openNamespace(preferences, kSystemNamespace, false)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    const bool ok = preferences.putBytes("theme_v1", data, length) == length;
    preferences.end();
    if(!ok) recordFailure(StorageDiagnosticCode::WriteFailed);
    return ok;
}

bool StorageService::saveThemeCache(const char * theme_id,
                                    const uint32_t palette[5]) {
    if(!theme_id || !theme_id[0] || !palette) return false;
    Preferences preferences;
    if(!openNamespace(preferences, kSystemNamespace, false)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    bool ok = preferences.putString("cache_theme", theme_id) > 0;
    const char * keys[] = {"th_bg", "th_surface", "th_primary",
                           "th_second", "th_critical"};
    for(uint8_t i = 0; i < 5 && ok; ++i) {
        ok = preferences.putUInt(keys[i], palette[i]) == sizeof(uint32_t);
    }
    preferences.end();
    if(!ok) recordFailure(StorageDiagnosticCode::WriteFailed);
    return ok;
}

void StorageService::applyLegacySnapshot(
    const LegacyStorageSnapshot & legacy,
    SystemSettings & settings,
    Alarm alarms[AlarmService::kSlots],
    bool present[AlarmService::kSlots]) {
    if(legacy.has_volume) settings.volume = legacy.volume > 100
        ? 100 : legacy.volume;
    for(uint8_t slot = 0; slot < AlarmService::kSlots; ++slot) {
        present[slot] = legacy.has_alarm[slot];
        if(present[slot]) alarms[slot] = legacy.alarms[slot];
    }
}

bool StorageService::migrateLegacyPreferences() {
    Preferences system;
    if(!openNamespace(system, kSystemNamespace, false)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    if(system.getBool("legacy_done", false)) {
        system.end();
        return true;
    }
    system.end();

    LegacyStorageSnapshot legacy{};
    Preferences old;
    if(openNamespace(old, kLegacyNamespace, true)) {
        legacy.has_volume = old.isKey("ui_volume");
        legacy.volume = old.getUChar("ui_volume", 50);
        for(uint8_t slot = 0; slot < AlarmService::kSlots; ++slot) {
            char key[20];
            legacyAlarmKey(key, sizeof(key), slot, "cfg");
            legacy.has_alarm[slot] = old.isKey(key);
            if(!legacy.has_alarm[slot]) continue;
            Alarm & alarm = legacy.alarms[slot];
            alarm.configured = old.getBool(key, false);
            legacyAlarmKey(key, sizeof(key), slot, "en");
            alarm.enabled = old.getBool(key, false);
            legacyAlarmKey(key, sizeof(key), slot, "hr");
            alarm.hour = old.getUChar(key, 7);
            legacyAlarmKey(key, sizeof(key), slot, "mn");
            alarm.minute = old.getUChar(key, 30);
            legacyAlarmKey(key, sizeof(key), slot, "dy");
            alarm.days_mask = old.getUChar(key, 0x7F);
            legacyAlarmKey(key, sizeof(key), slot, "rg");
            alarm.ringtone = old.getUChar(key, 0);
            legacyAlarmKey(key, sizeof(key), slot, "nm");
            const String name = old.getString(key, alarm.name);
            strlcpy(alarm.name, name.c_str(), sizeof(alarm.name));
        }
        if(!legacy.has_alarm[0] && old.getBool("alarm_on", false)) {
            legacy.has_alarm[0] = true;
            Alarm & alarm = legacy.alarms[0];
            alarm.configured = true;
            alarm.enabled = true;
            alarm.hour = old.getUChar("alarm_hour", 7);
            alarm.minute = old.getUChar("alarm_min", 30);
            strlcpy(alarm.name, "Alarm 1", sizeof(alarm.name));
        }
        old.end();
    }

    SystemSettings settings{};
    loadSettings(settings);
    Alarm alarms[AlarmService::kSlots]{};
    bool present[AlarmService::kSlots]{};
    applyLegacySnapshot(legacy, settings, alarms, present);
    bool ok = saveSettings(settings);
    for(uint8_t slot = 0; slot < AlarmService::kSlots && ok; ++slot) {
        if(present[slot]) ok = saveAlarm(slot, alarms[slot]);
    }
    if(ok && openNamespace(system, kSystemNamespace, false)) {
        ok = system.putBool("legacy_done", true) == sizeof(bool);
        system.end();
    } else {
        ok = false;
    }
    if(!ok) recordFailure(StorageDiagnosticCode::LegacyMigrationFailed);
    return ok;
}

bool StorageService::mountLittleFs() {
    littlefs_mounted_ = LittleFS.begin(false, "/littlefs", 4, "spiffs");
    littlefs_read_only_ = false;
    if(littlefs_mounted_) return true;

    const esp_vfs_littlefs_conf_t config = {
        .base_path = "/littlefs",
        .partition_label = "spiffs",
        .partition = nullptr,
        .format_if_mount_failed = false,
        .read_only = true,
        .dont_mount = false,
        .grow_on_mount = false,
    };
    littlefs_mounted_ = esp_vfs_littlefs_register(&config) == ESP_OK;
    littlefs_read_only_ = littlefs_mounted_;
    if(!littlefs_mounted_) {
        recordFailure(StorageDiagnosticCode::LittleFsMountFailed);
    }
    return littlefs_mounted_;
}

void StorageService::recordFailure(StorageDiagnosticCode code) {
    if(diagnostics_.failures < UINT32_MAX) ++diagnostics_.failures;
    diagnostics_.last = code;
}

}  // namespace firefly
