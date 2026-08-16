#pragma once

#include <stddef.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "PowerService.h"

namespace firefly {

enum class WifiPurpose : uint8_t {
    Ntp,
    Weather,
    Transfer,
    Ota,
    Count
};

enum class WifiMode : uint8_t {
    Off,
    Connecting,
    Connected,
    SoftAp,
    Error
};

enum class WifiLinkState : uint8_t {
    Off,
    Connecting,
    Connected,
    AuthFailed,
    NotFound,
    Failed
};

struct WifiCredentials {
    char ssid[33]{};
    char password[65]{};
    bool valid = false;
};

class WifiCredentialStore {
public:
    virtual ~WifiCredentialStore() = default;
    virtual bool loadWifiCredentials(WifiCredentials & output) = 0;
    virtual bool saveWifiCredentials(const WifiCredentials & credentials) = 0;
    virtual bool clearWifiCredentials() = 0;
};

enum class WifiProvisioningStatus : uint8_t {
    Idle,
    AwaitingUser,
    AwaitingForget,
    Connecting,
    Success,
    Forgotten,
    Denied,
    AuthFailed,
    NotFound,
    Timeout,
    PersistenceFailed,
    Busy,
    Invalid
};

struct WifiProvisioningSnapshot {
    WifiProvisioningStatus status = WifiProvisioningStatus::Idle;
    char ssid[33]{};
    uint32_t expires_at_ms = 0;
};

class WifiRadio {
public:
    virtual ~WifiRadio() = default;
    virtual bool probeHardware() { return true; }
    virtual bool hardwareAvailable() const { return true; }
    virtual bool connectStation(const char * ssid,
                                const char * password) = 0;
    virtual void useMinimumModemPowerSave() = 0;
    virtual void disconnectAndPowerOff() = 0;
    virtual WifiLinkState linkState() const = 0;
};

class WifiService {
public:
    static constexpr uint32_t kConnectionTimeoutMs = 15000;
    static constexpr uint32_t kDefaultIdleTimeoutMs = 60000;
    static constexpr uint32_t kLongSessionLimitMs = 15UL * 60UL * 1000UL;
    static constexpr uint8_t kMaxSsidBytes = 32;
    static constexpr uint8_t kMaxPasswordBytes = 64;
    static constexpr uint8_t kRememberedNonces = 8;

    WifiService();
    WifiService(WifiRadio & radio, PowerService & power);
    WifiService(WifiRadio & radio,
                PowerService & power,
                WifiCredentialStore & credential_store);

    void attachPowerService(PowerService & power);
    void attachCredentialStore(WifiCredentialStore & store);

    void configureTimeout(uint32_t timeout_ms);
    bool probeHardware();
    bool hardwareAvailable() const;
    bool request(WifiPurpose purpose, uint32_t now_ms);
    bool beginSoftApSession(WifiPurpose purpose, uint32_t now_ms);
    void release(WifiPurpose purpose, uint32_t now_ms);
    void tick(uint32_t now_ms);
    void onConnected(uint32_t now_ms);
    void onConnectionFailed();
    WifiMode mode() const;
    bool active(WifiPurpose purpose) const;
    bool provision(const char * ssid, const char * password);
    bool loadProvisionedNetwork();
    bool forgetNetwork();
    bool clearSensitiveState();
    bool provisioned() const;
    bool stageProvisioning(const uint8_t * payload,
                           size_t length,
                           uint32_t now_ms,
                           int64_t now_epoch);
    bool confirmProvisioning(bool allow, uint32_t now_ms);
    WifiProvisioningSnapshot provisioningSnapshot() const;
    bool ssidFingerprint(char output[9]) const;

private:
    static uint8_t purposeBit(WifiPurpose purpose);
    static bool isHighPower(WifiPurpose purpose);
    bool hasHighPowerSession() const;
    bool purposeAllowed(WifiPurpose purpose) const;
    void normalizeInactiveError();
    void setPowerSessionActive(bool active);
    void stop();
    void finishProvisioning(WifiProvisioningStatus status);
    static bool decodeProvisioning(const uint8_t * payload,
                                   size_t length,
                                   uint32_t now_ms,
                                   int64_t now_epoch,
                                   WifiCredentials & credentials,
                                   uint8_t nonce[8],
                                   uint32_t & nonce_expires_at_ms);

    WifiRadio * radio_ = nullptr;
    PowerService * power_ = nullptr;
    WifiCredentialStore * credential_store_ = nullptr;
    WifiMode mode_ = WifiMode::Off;
    uint8_t active_purposes_ = 0;
    uint32_t idle_timeout_ms_ = kDefaultIdleTimeoutMs;
    uint32_t session_started_ms_ = 0;
    uint32_t last_activity_ms_ = 0;
    char ssid_[kMaxSsidBytes + 1]{};
    char password_[kMaxPasswordBytes + 1]{};
    WifiCredentials pending_credentials_{};
    WifiProvisioningSnapshot provisioning_snapshot_{};
    uint8_t recent_nonces_[kRememberedNonces][8]{};
    uint32_t recent_nonce_expires_ms_[kRememberedNonces]{};
    bool recent_nonce_valid_[kRememberedNonces]{};
    uint8_t next_nonce_slot_ = 0;
    bool provisioning_connecting_ = false;
    bool pending_forget_ = false;
    mutable StaticSemaphore_t mutex_storage_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace firefly
