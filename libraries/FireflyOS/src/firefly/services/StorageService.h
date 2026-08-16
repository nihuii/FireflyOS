#pragma once

#include <stddef.h>
#include <stdint.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "AlarmService.h"
#include "WifiService.h"

namespace firefly {

class SdCardDevice;

struct SystemSettings {
    uint16_t schema_version = 1;
    uint8_t volume = 50;
    uint8_t brightness = 128;
    uint16_t auto_sleep_seconds = 30;
    bool hide_notification_content = true;
    bool wrist_raise_enabled = true;
    char theme_id[24] = "system-default";
};

struct ActivityStats {
    uint16_t schema_version = 1;
    uint32_t day_key = 0;
    uint32_t steps = 0;
    uint16_t active_minutes = 0;
};

struct PairingRecord {
    uint8_t app_token[16]{};
    char phone_name[33]{};
    bool valid = false;
    bool confirmed = false;
};

class PairingStore {
public:
    virtual ~PairingStore() = default;
    virtual bool loadPairing(PairingRecord & record) = 0;
    virtual bool savePairing(const PairingRecord & record) = 0;
    virtual bool clearPairing() = 0;
};

struct LegacyStorageSnapshot {
    bool has_volume = false;
    uint8_t volume = 50;
    bool has_alarm[AlarmService::kSlots]{};
    Alarm alarms[AlarmService::kSlots]{};
};

enum class StorageDiagnosticCode : uint8_t {
    None,
    NamespaceOpenFailed,
    SchemaMismatch,
    ReadFailed,
    WriteFailed,
    LegacyMigrationFailed,
    LittleFsMountFailed,
};

struct StorageDiagnostics {
    uint32_t failures = 0;
    StorageDiagnosticCode last = StorageDiagnosticCode::None;
};

class StorageService : public PairingStore, public WifiCredentialStore {
public:
    static constexpr uint16_t kSchemaVersion = 1;
    static constexpr const char * kSystemNamespace = "ff_sys";
    static constexpr const char * kAlarmNamespace = "ff_alarm";
    static constexpr const char * kPairNamespace = "ff_pair";
    static constexpr const char * kStatsNamespace = "ff_stats";
    static constexpr const char * kWifiNamespace = "ff_wifi";
    static constexpr const char * kLegacyNamespace = "firefly";

    bool begin();
    bool loadSettings(SystemSettings & settings);
    bool saveSettings(const SystemSettings & settings);
    bool loadAlarm(uint8_t slot, Alarm & alarm, bool & present);
    bool saveAlarm(uint8_t slot, const Alarm & alarm);
    bool loadActivityStats(ActivityStats & stats);
    bool saveActivityStats(const ActivityStats & stats);
    bool saveCompanionSettingRecord(uint8_t kind,
                                    const void * data,
                                    size_t length);
    bool loadCompanionSettingRecord(uint8_t kind,
                                    void * data,
                                    size_t capacity,
                                    size_t & length,
                                    bool & present);
    bool saveCompanionSettingsSnapshot(const void * data, size_t length);
    bool loadCompanionSettingsSnapshot(void * data,
                                       size_t capacity,
                                       size_t & length,
                                       bool & present);
    bool loadThemeTokens(void * data, size_t length);
    bool saveThemeTokens(const void * data, size_t length);
    bool saveThemeCache(const char * theme_id, const uint32_t palette[5]);
    bool loadThemeCache(char * theme_id, size_t id_size,
                        uint32_t palette[5], bool & present);
    bool clearThemeCache();
    bool loadPairing(PairingRecord & record) override;
    bool savePairing(const PairingRecord & record) override;
    bool clearPairing() override;
    bool loadWifiCredentials(WifiCredentials & credentials) override;
    bool saveWifiCredentials(const WifiCredentials & credentials) override;
    bool clearWifiCredentials() override;
    bool clearInternalUserData();
    bool clearManagedSdRoot();

    void attachSd(fs::FS & filesystem, SdCardDevice & device);
    void detachSd();
    bool sdAvailable() const;
    bool validateSdSession();
    uint64_t sdTotalBytes();
    uint64_t sdUsedBytes();
    fs::File openManaged(const char * path, const char * mode = FILE_READ);
    fs::File openNextManaged(fs::File & directory);
    bool managedFileName(fs::File & file, char * out, size_t out_size);
    bool managedFilePath(fs::File & file, char * out, size_t out_size);
    bool managedFileSize(fs::File & file, uint64_t & size);
    bool managedFileIsDirectory(fs::File & file, bool & directory);
    bool managedExists(const char * path);
    bool removeManaged(const char * path);
    bool renameManaged(const char * from, const char * to);
    size_t readManaged(fs::File & file, uint8_t * data, size_t length);
    size_t writeManaged(fs::File & file, const uint8_t * data, size_t length);
    bool seekManaged(fs::File & file, uint32_t position);
    void closeManaged(fs::File & file);
    uint16_t cleanupBulkPartFiles();
    bool beginBulkSdSession();
    void endBulkSdSession();
    bool bulkSdSessionActive() const;
    bool bulkSdAvailable();
    uint64_t bulkSdFreeBytes();
    fs::File openBulkManaged(const char * path,
                             const char * mode = FILE_READ);
    bool bulkManagedExists(const char * path);
    bool removeBulkManaged(const char * path);
    bool renameBulkManaged(const char * from, const char * to);
    size_t writeBulkManaged(fs::File & file,
                            const uint8_t * data,
                            size_t length);
    size_t readBulkManaged(fs::File & file,
                           uint8_t * data,
                           size_t length);
    void closeBulkManaged(fs::File & file);
    bool beginOtaSdSession();
    void endOtaSdSession();
    bool otaSdAvailable();
    fs::File openOtaManaged(const char * path);
    size_t readOtaManaged(fs::File & file,
                          uint8_t * data,
                          size_t length);
    void closeOtaManaged(fs::File & file);
    void reportSdResult(bool success);
    static bool isManagedPath(const char * path);

    bool littleFsMounted() const { return littlefs_mounted_; }
    bool littleFsReadOnly() const { return littlefs_read_only_; }
    StorageDiagnostics diagnostics() const { return diagnostics_; }

    static void applyLegacySnapshot(const LegacyStorageSnapshot & legacy,
                                    SystemSettings & settings,
                                    Alarm alarms[AlarmService::kSlots],
                                    bool present[AlarmService::kSlots]);

private:
    bool initializeNamespaces();
    bool migrateLegacyPreferences();
    bool mountLittleFs();
    bool takeSdLock(TickType_t timeout = pdMS_TO_TICKS(50),
                    bool bulk_owner = false);
    void giveSdLock();
    void recordFailure(StorageDiagnosticCode code);

    bool littlefs_mounted_ = false;
    bool littlefs_read_only_ = false;
    fs::FS * sd_filesystem_ = nullptr;
    SdCardDevice * sd_device_ = nullptr;
    mutable SemaphoreHandle_t sd_mutex_ = nullptr;
    bool bulk_sd_session_ = false;
    bool ota_sd_session_ = false;
    uint16_t normal_sd_handles_ = 0;
    StorageDiagnostics diagnostics_{};
};

}  // namespace firefly
