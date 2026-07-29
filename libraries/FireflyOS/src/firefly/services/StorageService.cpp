#include "StorageService.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_littlefs.h>
#include <string.h>

#include "../hal/SdCardDevice.h"

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

constexpr uint8_t kPairPhaseProvisional = 1;
constexpr uint8_t kPairPhaseConfirmed = 2;

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

bool StorageService::saveCompanionSettingRecord(uint8_t kind,
                                                const void * data,
                                                size_t length) {
    if(kind < 1 || kind > 4 || !data || length == 0 || length > 320) {
        return false;
    }
    Preferences preferences;
    if(!openNamespace(preferences, kSystemNamespace, false)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    char key[8];
    snprintf(key, sizeof(key), "sync_%u", static_cast<unsigned>(kind));
    const bool ok = writeSchema(preferences) &&
        preferences.putBytes(key, data, length) == length;
    preferences.end();
    if(!ok) recordFailure(StorageDiagnosticCode::WriteFailed);
    return ok;
}

bool StorageService::loadCompanionSettingRecord(uint8_t kind,
                                                void * data,
                                                size_t capacity,
                                                size_t & length,
                                                bool & present) {
    length = 0;
    present = false;
    if(kind < 1 || kind > 4 || !data || capacity == 0 || capacity > 320) {
        return false;
    }
    Preferences preferences;
    if(!openNamespace(preferences, kSystemNamespace, true)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    char key[8];
    snprintf(key, sizeof(key), "sync_%u", static_cast<unsigned>(kind));
    const size_t stored = preferences.getBytesLength(key);
    present = stored > 0;
    const bool ok = !present || (stored <= capacity &&
        preferences.getBytes(key, data, stored) == stored);
    preferences.end();
    if(!ok) {
        recordFailure(StorageDiagnosticCode::ReadFailed);
        return false;
    }
    length = stored;
    return true;
}

bool StorageService::saveCompanionSettingsSnapshot(const void * data,
                                                   size_t length) {
    if(!data || length == 0 || length > 1200) return false;
    Preferences preferences;
    if(!openNamespace(preferences, kSystemNamespace, false)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    const bool ok = writeSchema(preferences) &&
        preferences.putBytes("sync_all", data, length) == length;
    preferences.end();
    if(!ok) recordFailure(StorageDiagnosticCode::WriteFailed);
    return ok;
}

bool StorageService::loadCompanionSettingsSnapshot(void * data,
                                                   size_t capacity,
                                                   size_t & length,
                                                   bool & present) {
    length = 0;
    present = false;
    if(!data || capacity == 0 || capacity > 1200) return false;
    Preferences preferences;
    if(!openNamespace(preferences, kSystemNamespace, true)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    const size_t stored = preferences.getBytesLength("sync_all");
    present = stored > 0;
    const bool ok = !present || (stored <= capacity &&
        preferences.getBytes("sync_all", data, stored) == stored);
    preferences.end();
    if(!ok) {
        recordFailure(StorageDiagnosticCode::ReadFailed);
        return false;
    }
    length = stored;
    return true;
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

bool StorageService::loadThemeCache(char * theme_id,
                                    size_t id_size,
                                    uint32_t palette[5],
                                    bool & present) {
    if(!theme_id || id_size == 0 || !palette) return false;
    theme_id[0] = '\0';
    memset(palette, 0, sizeof(uint32_t) * 5);
    present = false;
    Preferences preferences;
    if(!openNamespace(preferences, kSystemNamespace, true)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    present = preferences.isKey("cache_theme");
    bool ok = true;
    if(present) {
        const String cached = preferences.getString("cache_theme", "");
        strlcpy(theme_id, cached.c_str(), id_size);
        const char * keys[] = {"th_bg", "th_surface", "th_primary",
                              "th_second", "th_critical"};
        for(uint8_t i = 0; i < 5; ++i) {
            if(!preferences.isKey(keys[i])) {
                ok = false;
                break;
            }
            palette[i] = preferences.getUInt(keys[i], 0);
        }
    }
    preferences.end();
    if(!ok) recordFailure(StorageDiagnosticCode::ReadFailed);
    return ok;
}

bool StorageService::clearThemeCache() {
    Preferences preferences;
    if(!openNamespace(preferences, kSystemNamespace, false)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    const char * keys[] = {"cache_theme", "th_bg", "th_surface",
                          "th_primary", "th_second", "th_critical"};
    bool ok = true;
    for(const char * key : keys) {
        if(preferences.isKey(key) && !preferences.remove(key)) ok = false;
    }
    preferences.end();
    if(!ok) recordFailure(StorageDiagnosticCode::WriteFailed);
    return ok;
}

bool StorageService::loadPairing(PairingRecord & record) {
    record = PairingRecord{};
    Preferences preferences;
    if(!openNamespace(preferences, kPairNamespace, true)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    if(preferences.getUShort("schema", 0) != kSchemaVersion) {
        preferences.end();
        recordFailure(StorageDiagnosticCode::SchemaMismatch);
        return false;
    }
    const size_t token_length = preferences.getBytesLength("token");
    const size_t phone_length = preferences.getBytesLength("phone");
    const bool has_phase = preferences.isKey("phase");
    const uint8_t phase = preferences.getUChar(
        "phase", kPairPhaseConfirmed
    );
    if(token_length == 0 && phone_length == 0) {
        preferences.end();
        return true;
    }
    const bool lengths_valid = token_length == sizeof(record.app_token) &&
        phone_length > 1 && phone_length <= sizeof(record.phone_name);
    const bool phase_valid = !has_phase ||
        phase == kPairPhaseProvisional ||
        phase == kPairPhaseConfirmed;
    const bool read_ok = lengths_valid &&
        phase_valid &&
        preferences.getBytes("token", record.app_token,
                             sizeof(record.app_token)) == sizeof(record.app_token) &&
        preferences.getBytes("phone", record.phone_name, phone_length) == phone_length;
    preferences.end();
    if(!read_ok || record.phone_name[phone_length - 1] != '\0') {
        record = PairingRecord{};
        recordFailure(StorageDiagnosticCode::ReadFailed);
        return false;
    }
    record.phone_name[sizeof(record.phone_name) - 1] = '\0';
    record.valid = true;
    record.confirmed = !has_phase || phase == kPairPhaseConfirmed;
    return true;
}

bool StorageService::savePairing(const PairingRecord & record) {
    const size_t phone_length = strnlen(record.phone_name,
                                        sizeof(record.phone_name));
    if(!record.valid || phone_length == 0 ||
       phone_length >= sizeof(record.phone_name)) {
        return false;
    }
    Preferences preferences;
    if(!openNamespace(preferences, kPairNamespace, false)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    const bool ok = writeSchema(preferences) &&
        preferences.putUChar("phase", kPairPhaseProvisional) ==
            sizeof(uint8_t) &&
        preferences.putBytes("token", record.app_token,
                             sizeof(record.app_token)) == sizeof(record.app_token) &&
        preferences.putBytes("phone", record.phone_name,
                             phone_length + 1) == phone_length + 1 &&
        (!record.confirmed ||
         preferences.putUChar("phase", kPairPhaseConfirmed) ==
            sizeof(uint8_t));
    if(!ok) {
        preferences.remove("phase");
        preferences.remove("token");
        preferences.remove("phone");
    }
    preferences.end();
    if(!ok) recordFailure(StorageDiagnosticCode::WriteFailed);
    return ok;
}

bool StorageService::clearPairing() {
    Preferences preferences;
    if(!openNamespace(preferences, kPairNamespace, false)) {
        recordFailure(StorageDiagnosticCode::NamespaceOpenFailed);
        return false;
    }
    bool ok = writeSchema(preferences);
    if(preferences.isKey("phase")) ok = preferences.remove("phase") && ok;
    if(preferences.isKey("token")) ok = preferences.remove("token") && ok;
    if(preferences.isKey("phone")) ok = preferences.remove("phone") && ok;
    preferences.end();
    if(!ok) recordFailure(StorageDiagnosticCode::WriteFailed);
    return ok;
}

void StorageService::attachSd(fs::FS & filesystem, SdCardDevice & device) {
    if(!sd_mutex_) sd_mutex_ = xSemaphoreCreateMutex();
    sd_filesystem_ = &filesystem;
    sd_device_ = &device;
}

void StorageService::detachSd() {
    sd_filesystem_ = nullptr;
    sd_device_ = nullptr;
}

bool StorageService::sdAvailable() const {
    return sd_filesystem_ && sd_device_ && sd_device_->mounted();
}

bool StorageService::validateSdSession() {
    if(!sd_device_ || !takeSdLock()) return false;
    const bool available = sd_device_->validateSession();
    giveSdLock();
    return available;
}

uint64_t StorageService::sdTotalBytes() {
    if(!sdAvailable() || !takeSdLock()) return 0;
    const uint64_t total = sd_device_->totalBytes();
    giveSdLock();
    return total;
}

uint64_t StorageService::sdUsedBytes() {
    if(!sdAvailable() || !takeSdLock()) return 0;
    const uint64_t used = sd_device_->usedBytes();
    giveSdLock();
    return used;
}

bool StorageService::isManagedPath(const char * path) {
    constexpr const char * root = "/FireflyOS";
    constexpr size_t root_length = 10;
    if(!path || strncmp(path, root, root_length) != 0 ||
       (path[root_length] != '\0' && path[root_length] != '/')) {
        return false;
    }
    const char * component = path + root_length;
    if(*component == '/') ++component;
    if(*component) {
        static const char * const names[] = {
            "Music", "Recordings", "Pictures", "Themes",
            "Updates", "Backups", "Logs",
        };
        bool managed_top_level = false;
        for(const char * name : names) {
            const size_t length = strlen(name);
            if(strncmp(component, name, length) == 0 &&
               (component[length] == '\0' || component[length] == '/')) {
                managed_top_level = true;
                break;
            }
        }
        if(!managed_top_level) return false;
    }
    for(const char * cursor = component; ; ++cursor) {
        const char value = *cursor;
        if(value == '\\' || value == ':' ||
           (static_cast<unsigned char>(value) < 0x20 && value != '\0')) {
            return false;
        }
        if(value == '/' || value == '\0') {
            const size_t length = static_cast<size_t>(cursor - component);
            if((length == 1 && component[0] == '.') ||
               (length == 2 && component[0] == '.' && component[1] == '.')) {
                return false;
            }
            if(value == '\0') break;
            if(length == 0) return false;
            component = cursor + 1;
        }
    }
    return true;
}

bool StorageService::takeSdLock(TickType_t timeout) {
    return sd_mutex_ && xSemaphoreTake(sd_mutex_, timeout) == pdTRUE;
}

void StorageService::giveSdLock() {
    if(sd_mutex_) xSemaphoreGive(sd_mutex_);
}

fs::File StorageService::openManaged(const char * path, const char * mode) {
    if(!sdAvailable() || !isManagedPath(path) || !takeSdLock()) return {};
    fs::File file = sd_filesystem_->open(path, mode);
    if(!file && sd_device_) sd_device_->validateSession();
    else if(file && sd_device_) sd_device_->noteIoResult(true);
    giveSdLock();
    return file;
}

fs::File StorageService::openNextManaged(fs::File & directory) {
    if(!sdAvailable() || !directory || !takeSdLock()) return {};
    fs::File entry = directory.openNextFile();
    if(!entry && sd_device_) sd_device_->validateSession();
    else if(entry && sd_device_) sd_device_->noteIoResult(true);
    giveSdLock();
    return entry;
}

bool StorageService::managedFileName(fs::File & file,
                                     char * out,
                                     size_t out_size) {
    if(!sdAvailable() || !file || !out || out_size == 0 || !takeSdLock()) {
        return false;
    }
    const char * name = file.name();
    const bool success = name && name[0];
    if(success) strlcpy(out, name, out_size);
    else out[0] = '\0';
    if(sd_device_) sd_device_->noteIoResult(success);
    giveSdLock();
    return success;
}

bool StorageService::managedFilePath(fs::File & file,
                                     char * out,
                                     size_t out_size) {
    if(!sdAvailable() || !file || !out || out_size == 0 || !takeSdLock()) {
        return false;
    }
    const char * path = file.path();
    const bool success = path && path[0] && isManagedPath(path);
    if(success) strlcpy(out, path, out_size);
    else out[0] = '\0';
    if(sd_device_) sd_device_->noteIoResult(success);
    giveSdLock();
    return success;
}

bool StorageService::managedFileSize(fs::File & file, uint64_t & size) {
    size = 0;
    if(!sdAvailable() || !file || !takeSdLock()) return false;
    size = file.size();
    if(sd_device_) sd_device_->noteIoResult(true);
    giveSdLock();
    return true;
}

bool StorageService::managedFileIsDirectory(fs::File & file, bool & directory) {
    directory = false;
    if(!sdAvailable() || !file || !takeSdLock()) return false;
    directory = file.isDirectory();
    if(sd_device_) sd_device_->noteIoResult(true);
    giveSdLock();
    return true;
}

bool StorageService::managedExists(const char * path) {
    if(!sdAvailable() || !isManagedPath(path) || !takeSdLock()) return false;
    const bool present = sd_filesystem_->exists(path);
    if(sd_device_) sd_device_->validateSession();
    giveSdLock();
    return present;
}

bool StorageService::removeManaged(const char * path) {
    if(!sdAvailable() || !isManagedPath(path) || !takeSdLock()) return false;
    const bool success = sd_filesystem_->remove(path);
    if(sd_device_) sd_device_->noteIoResult(success);
    giveSdLock();
    return success;
}

bool StorageService::renameManaged(const char * from, const char * to) {
    if(!sdAvailable() || !isManagedPath(from) || !isManagedPath(to) ||
       !takeSdLock()) return false;
    const bool success = sd_filesystem_->rename(from, to);
    if(sd_device_) sd_device_->noteIoResult(success);
    giveSdLock();
    return success;
}

size_t StorageService::readManaged(fs::File & file,
                                   uint8_t * data,
                                   size_t length) {
    if(!sdAvailable() || !file || !data || length == 0 || !takeSdLock()) return 0;
    const size_t read = file.read(data, length);
    if(sd_device_) sd_device_->noteIoResult(read == length);
    giveSdLock();
    return read;
}

size_t StorageService::writeManaged(fs::File & file,
                                    const uint8_t * data,
                                    size_t length) {
    if(!sdAvailable() || !file || !data || length == 0 || !takeSdLock()) return 0;
    const size_t written = file.write(data, length);
    if(sd_device_) sd_device_->noteIoResult(written == length);
    giveSdLock();
    return written;
}

bool StorageService::seekManaged(fs::File & file, uint32_t position) {
    if(!sdAvailable() || !file || !takeSdLock()) return false;
    const bool success = file.seek(position);
    if(sd_device_) sd_device_->noteIoResult(success);
    giveSdLock();
    return success;
}

void StorageService::closeManaged(fs::File & file) {
    if(!file) return;
    if(takeSdLock()) {
        file.close();
        giveSdLock();
    } else {
        file.close();
    }
}

void StorageService::reportSdResult(bool success) {
    if(sd_device_) sd_device_->noteIoResult(success);
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
