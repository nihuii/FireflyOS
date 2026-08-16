#include "WifiService.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
#include <string.h>

namespace firefly {
namespace {

class Esp32WifiRadio final : public WifiRadio {
public:
    bool probeHardware() override {
        hardware_available_ = WiFi.mode(WIFI_STA);
        if(hardware_available_) hardware_available_ = WiFi.mode(WIFI_OFF);
        return hardware_available_;
    }

    bool hardwareAvailable() const override { return hardware_available_; }

    bool connectStation(const char * ssid, const char * password) override {
        hardware_available_ = WiFi.mode(WIFI_STA);
        if(!hardware_available_) return false;
        return WiFi.begin(ssid, password) != WL_CONNECT_FAILED;
    }

    void useMinimumModemPowerSave() override {
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    }

    void disconnectAndPowerOff() override {
        WiFi.disconnect(true);
        hardware_available_ = WiFi.mode(WIFI_OFF);
    }

    WifiLinkState linkState() const override {
        switch(WiFi.status()) {
            case WL_CONNECTED: return WifiLinkState::Connected;
            case WL_CONNECT_FAILED: return WifiLinkState::AuthFailed;
            case WL_NO_SSID_AVAIL: return WifiLinkState::NotFound;
            case WL_IDLE_STATUS:
            case WL_DISCONNECTED: return WifiLinkState::Connecting;
            default: return WifiLinkState::Failed;
        }
    }

private:
    bool hardware_available_ = true;
};

Esp32WifiRadio & defaultRadio() {
    static Esp32WifiRadio radio;
    return radio;
}

size_t boundedLength(const char * value, size_t maximum) {
    if(!value) return maximum + 1;
    size_t length = 0;
    while(length <= maximum && value[length] != '\0') ++length;
    return length;
}

class RecursiveLock {
public:
    explicit RecursiveLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        locked_ = !mutex_ ||
            xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) == pdTRUE;
    }
    ~RecursiveLock() {
        if(locked_ && mutex_) xSemaphoreGiveRecursive(mutex_);
    }
private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
};

}  // namespace

WifiService::WifiService() : radio_(&defaultRadio()) {
    mutex_ = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage_);
}

WifiService::WifiService(WifiRadio & radio, PowerService & power)
    : radio_(&radio), power_(&power) {
    mutex_ = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage_);
}

WifiService::WifiService(WifiRadio & radio,
                         PowerService & power,
                         WifiCredentialStore & credential_store)
    : radio_(&radio), power_(&power), credential_store_(&credential_store) {
    mutex_ = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage_);
}

void WifiService::attachPowerService(PowerService & power) {
    RecursiveLock lock(mutex_);
    power_ = &power;
}

void WifiService::attachCredentialStore(WifiCredentialStore & store) {
    RecursiveLock lock(mutex_);
    credential_store_ = &store;
}

void WifiService::configureTimeout(uint32_t timeout_ms) {
    RecursiveLock lock(mutex_);
    idle_timeout_ms_ = timeout_ms == 0 ? kDefaultIdleTimeoutMs : timeout_ms;
}

bool WifiService::probeHardware() {
    RecursiveLock lock(mutex_);
    return radio_ && radio_->probeHardware();
}

bool WifiService::hardwareAvailable() const {
    RecursiveLock lock(mutex_);
    return radio_ && radio_->hardwareAvailable();
}

uint8_t WifiService::purposeBit(WifiPurpose purpose) {
    const uint8_t index = static_cast<uint8_t>(purpose);
    return index < static_cast<uint8_t>(WifiPurpose::Count)
        ? static_cast<uint8_t>(1U << index)
        : 0;
}

bool WifiService::isHighPower(WifiPurpose purpose) {
    return purpose == WifiPurpose::Transfer || purpose == WifiPurpose::Ota;
}

bool WifiService::hasHighPowerSession() const {
    return active(WifiPurpose::Transfer) || active(WifiPurpose::Ota);
}

bool WifiService::purposeAllowed(WifiPurpose purpose) const {
    const uint8_t bit = purposeBit(purpose);
    if(bit == 0 || !provisioned()) return false;
    return !power_ || power_->allowsWifiSession(isHighPower(purpose));
}

void WifiService::normalizeInactiveError() {
    if(mode_ == WifiMode::Error && active_purposes_ == 0) {
        mode_ = WifiMode::Off;
    }
}

bool WifiService::request(WifiPurpose purpose, uint32_t now_ms) {
    RecursiveLock lock(mutex_);
    normalizeInactiveError();
    const uint8_t bit = purposeBit(purpose);
    if(mode_ == WifiMode::SoftAp) {
        return bit != 0 && (active_purposes_ & bit) != 0;
    }
    if(!purposeAllowed(purpose) || !radio_) return false;

    if((active_purposes_ & bit) != 0) {
        last_activity_ms_ = now_ms;
        return true;
    }

    active_purposes_ |= bit;
    last_activity_ms_ = now_ms;
    if(mode_ == WifiMode::Connected || mode_ == WifiMode::Connecting) {
        return true;
    }

    session_started_ms_ = now_ms;
    mode_ = WifiMode::Connecting;
    setPowerSessionActive(true);
    if(radio_->connectStation(ssid_, password_)) return true;

    active_purposes_ = 0;
    mode_ = WifiMode::Error;
    setPowerSessionActive(false);
    radio_->disconnectAndPowerOff();
    return false;
}

bool WifiService::beginSoftApSession(WifiPurpose purpose, uint32_t now_ms) {
    RecursiveLock lock(mutex_);
    normalizeInactiveError();
    if(purpose != WifiPurpose::Transfer ||
       mode_ != WifiMode::Off || active_purposes_ != 0 ||
       (power_ && !power_->allowsWifiSession(true))) return false;
    active_purposes_ |= purposeBit(purpose);
    session_started_ms_ = now_ms;
    last_activity_ms_ = now_ms;
    mode_ = WifiMode::SoftAp;
    setPowerSessionActive(true);
    return true;
}

void WifiService::release(WifiPurpose purpose, uint32_t now_ms) {
    RecursiveLock lock(mutex_);
    const uint8_t bit = purposeBit(purpose);
    if(bit == 0) return;
    active_purposes_ &= static_cast<uint8_t>(~bit);
    last_activity_ms_ = now_ms;
    if(active_purposes_ == 0) stop();
}

void WifiService::tick(uint32_t now_ms) {
    RecursiveLock lock(mutex_);
    const bool awaiting_confirmation =
        provisioning_snapshot_.status == WifiProvisioningStatus::AwaitingUser ||
        provisioning_snapshot_.status == WifiProvisioningStatus::AwaitingForget;
    if(awaiting_confirmation &&
       static_cast<int32_t>(provisioning_snapshot_.expires_at_ms - now_ms) < 0) {
        finishProvisioning(WifiProvisioningStatus::Timeout);
    }
    if(mode_ == WifiMode::Connecting && radio_) {
        const WifiLinkState link = radio_->linkState();
        if(link == WifiLinkState::Connected) {
            onConnected(now_ms);
            return;
        }
        if(link == WifiLinkState::AuthFailed) {
            if(provisioning_connecting_) {
                stop();
                finishProvisioning(WifiProvisioningStatus::AuthFailed);
            } else {
                onConnectionFailed();
            }
            return;
        }
        if(link == WifiLinkState::NotFound) {
            if(provisioning_connecting_) {
                stop();
                finishProvisioning(WifiProvisioningStatus::NotFound);
            } else {
                onConnectionFailed();
            }
            return;
        }
    }
    if(mode_ == WifiMode::Connecting &&
       now_ms - session_started_ms_ >= kConnectionTimeoutMs) {
        const bool was_provisioning = provisioning_connecting_;
        stop();
        if(was_provisioning) finishProvisioning(WifiProvisioningStatus::Timeout);
        return;
    }
    if(hasHighPowerSession() &&
       now_ms - session_started_ms_ >= kLongSessionLimitMs) {
        stop();
        return;
    }
    if(mode_ != WifiMode::Connected) return;

    if(radio_ && radio_->linkState() != WifiLinkState::Connected) {
        onConnectionFailed();
        return;
    }

    if(hasHighPowerSession()) return;
    if(now_ms - last_activity_ms_ >= idle_timeout_ms_) stop();
}

void WifiService::onConnected(uint32_t now_ms) {
    RecursiveLock lock(mutex_);
    if(mode_ != WifiMode::Connecting || !radio_) {
        return;
    }
    if(provisioning_connecting_) {
        radio_->useMinimumModemPowerSave();
        const bool saved = !credential_store_ ||
            credential_store_->saveWifiCredentials(pending_credentials_);
        if(saved) {
            provision(pending_credentials_.ssid, pending_credentials_.password);
        }
        pending_credentials_ = {};
        provisioning_connecting_ = false;
        stop();
        finishProvisioning(saved
            ? WifiProvisioningStatus::Success
            : WifiProvisioningStatus::PersistenceFailed);
        return;
    }
    if(active_purposes_ == 0) return;
    mode_ = WifiMode::Connected;
    last_activity_ms_ = now_ms;
    radio_->useMinimumModemPowerSave();
}

void WifiService::onConnectionFailed() {
    RecursiveLock lock(mutex_);
    if(mode_ == WifiMode::Off) return;
    stop();
    mode_ = WifiMode::Error;
}

bool WifiService::active(WifiPurpose purpose) const {
    RecursiveLock lock(mutex_);
    const uint8_t bit = purposeBit(purpose);
    return bit != 0 && (active_purposes_ & bit) != 0;
}

bool WifiService::provision(const char * ssid, const char * password) {
    RecursiveLock lock(mutex_);
    const size_t ssid_length = boundedLength(ssid, kMaxSsidBytes);
    const size_t password_length = boundedLength(password, kMaxPasswordBytes);
    if(ssid_length == 0 || ssid_length > kMaxSsidBytes ||
       password_length > kMaxPasswordBytes) {
        return false;
    }

    memcpy(ssid_, ssid, ssid_length);
    ssid_[ssid_length] = '\0';
    memcpy(password_, password, password_length);
    password_[password_length] = '\0';
    return true;
}

bool WifiService::loadProvisionedNetwork() {
    RecursiveLock lock(mutex_);
    if(!credential_store_) return provisioned();
    WifiCredentials credentials{};
    if(!credential_store_->loadWifiCredentials(credentials)) return false;
    if(!credentials.valid) {
        memset(ssid_, 0, sizeof(ssid_));
        memset(password_, 0, sizeof(password_));
        return true;
    }
    return provision(credentials.ssid, credentials.password);
}

bool WifiService::forgetNetwork() {
    return clearSensitiveState();
}

bool WifiService::clearSensitiveState() {
    RecursiveLock lock(mutex_);
    stop();
    const bool durable_cleared = !credential_store_ ||
        credential_store_->clearWifiCredentials();
    memset(ssid_, 0, sizeof(ssid_));
    memset(password_, 0, sizeof(password_));
    pending_credentials_ = {};
    provisioning_connecting_ = false;
    pending_forget_ = false;
    provisioning_snapshot_ = {};
    memset(recent_nonces_, 0, sizeof(recent_nonces_));
    memset(recent_nonce_expires_ms_, 0, sizeof(recent_nonce_expires_ms_));
    memset(recent_nonce_valid_, 0, sizeof(recent_nonce_valid_));
    next_nonce_slot_ = 0;
    return durable_cleared;
}

bool WifiService::decodeProvisioning(const uint8_t * payload,
                                     size_t length,
                                     uint32_t now_ms,
                                     int64_t now_epoch,
                                     WifiCredentials & credentials,
                                     uint8_t nonce[8],
                                     uint32_t & nonce_expires_at_ms) {
    credentials = {};
    nonce_expires_at_ms = 0;
    if(!payload || length == 0 || !nonce) return false;
    size_t offset = 0;
    uint32_t ttl_ms = 0;
    if(payload[0] == 1) {
        if(length < 19 || now_epoch <= 0) return false;
        uint64_t raw_expiry = 0;
        for(uint8_t index = 0; index < 8; ++index) {
            raw_expiry |= static_cast<uint64_t>(payload[1 + index]) <<
                (index * 8);
        }
        const int64_t expiry = static_cast<int64_t>(raw_expiry);
        if(expiry < now_epoch || expiry > now_epoch + 60) return false;
        ttl_ms = static_cast<uint32_t>(expiry - now_epoch) * 1000UL;
        offset = 9;
    } else if(payload[0] == 2) {
        if(length < 12 || payload[1] == 0 || payload[1] > 60) return false;
        ttl_ms = static_cast<uint32_t>(payload[1]) * 1000UL;
        offset = 2;
    } else {
        return false;
    }
    memcpy(nonce, payload + offset, 8);
    offset += 8;
    const uint8_t ssid_length = payload[offset++];
    if(ssid_length == 0 || ssid_length > kMaxSsidBytes ||
       offset + ssid_length + 1 > length) {
        return false;
    }
    memcpy(credentials.ssid, payload + offset, ssid_length);
    credentials.ssid[ssid_length] = '\0';
    offset += ssid_length;
    const uint8_t password_length = payload[offset++];
    if(password_length > kMaxPasswordBytes ||
       offset + password_length != length) {
        credentials = {};
        return false;
    }
    memcpy(credentials.password, payload + offset, password_length);
    credentials.password[password_length] = '\0';
    credentials.valid = true;
    nonce_expires_at_ms = now_ms + ttl_ms;
    return true;
}

bool WifiService::stageProvisioning(const uint8_t * payload,
                                    size_t length,
                                    uint32_t now_ms,
                                    int64_t now_epoch) {
    RecursiveLock lock(mutex_);
    if(provisioning_snapshot_.status == WifiProvisioningStatus::AwaitingUser ||
       provisioning_snapshot_.status == WifiProvisioningStatus::AwaitingForget ||
       provisioning_snapshot_.status == WifiProvisioningStatus::Connecting ||
       provisioning_connecting_) {
        return false;
    }
    if(payload && length == 2 &&
       (payload[0] == 1 || payload[0] == 2) && payload[1] == 0) {
        if(!provisioned()) {
            provisioning_snapshot_.status = WifiProvisioningStatus::Invalid;
            return false;
        }
        pending_forget_ = true;
        provisioning_snapshot_ = {};
        provisioning_snapshot_.status = WifiProvisioningStatus::AwaitingForget;
        strlcpy(provisioning_snapshot_.ssid, ssid_,
                sizeof(provisioning_snapshot_.ssid));
        provisioning_snapshot_.expires_at_ms = now_ms + 60000UL;
        return true;
    }
    pending_forget_ = false;
    WifiCredentials credentials{};
    uint8_t nonce[8]{};
    uint32_t nonce_expires_at_ms = 0;
    bool replayed = false;
    int8_t available_nonce_slot = -1;
    if(decodeProvisioning(payload, length, now_ms, now_epoch, credentials,
                           nonce, nonce_expires_at_ms)) {
        for(uint8_t offset = 0; offset < kRememberedNonces; ++offset) {
            const uint8_t index = static_cast<uint8_t>(
                (next_nonce_slot_ + offset) % kRememberedNonces);
            const bool live = recent_nonce_valid_[index] &&
                static_cast<int32_t>(recent_nonce_expires_ms_[index] -
                                     now_ms) >= 0;
            if(live &&
               memcmp(recent_nonces_[index], nonce, sizeof(nonce)) == 0) {
                replayed = true;
                break;
            }
            if(!live && available_nonce_slot < 0) {
                available_nonce_slot = static_cast<int8_t>(index);
            }
        }
    }
    if(!credentials.valid || replayed || available_nonce_slot < 0) {
        provisioning_snapshot_.status = WifiProvisioningStatus::Invalid;
        pending_credentials_ = {};
        pending_forget_ = false;
        credentials = {};
        return false;
    }
    const uint8_t nonce_slot = static_cast<uint8_t>(available_nonce_slot);
    memcpy(recent_nonces_[nonce_slot], nonce, sizeof(nonce));
    recent_nonce_expires_ms_[nonce_slot] = nonce_expires_at_ms;
    recent_nonce_valid_[nonce_slot] = true;
    next_nonce_slot_ = static_cast<uint8_t>(
        (nonce_slot + 1) % kRememberedNonces);
    pending_credentials_ = credentials;
    provisioning_snapshot_ = {};
    provisioning_snapshot_.status = WifiProvisioningStatus::AwaitingUser;
    strlcpy(provisioning_snapshot_.ssid, credentials.ssid,
            sizeof(provisioning_snapshot_.ssid));
    provisioning_snapshot_.expires_at_ms = nonce_expires_at_ms;
    return true;
}

bool WifiService::confirmProvisioning(bool allow, uint32_t now_ms) {
    RecursiveLock lock(mutex_);
    const bool awaiting_credentials =
        provisioning_snapshot_.status == WifiProvisioningStatus::AwaitingUser;
    const bool awaiting_forget =
        provisioning_snapshot_.status == WifiProvisioningStatus::AwaitingForget;
    if((!awaiting_credentials && !awaiting_forget) ||
       static_cast<int32_t>(provisioning_snapshot_.expires_at_ms - now_ms) < 0) {
        finishProvisioning(WifiProvisioningStatus::Timeout);
        return false;
    }
    if(!allow) {
        pending_credentials_ = {};
        pending_forget_ = false;
        finishProvisioning(WifiProvisioningStatus::Denied);
        return true;
    }
    normalizeInactiveError();
    if(mode_ != WifiMode::Off || active_purposes_ != 0) {
        finishProvisioning(WifiProvisioningStatus::Busy);
        return false;
    }
    if(awaiting_forget && pending_forget_) {
        const bool cleared = forgetNetwork();
        provisioning_snapshot_.status = cleared
            ? WifiProvisioningStatus::Forgotten
            : WifiProvisioningStatus::PersistenceFailed;
        return cleared;
    }
    if(!radio_ || !pending_credentials_.valid ||
       (power_ && !power_->allowsWifiSession(false))) {
        finishProvisioning(WifiProvisioningStatus::Invalid);
        return false;
    }
    mode_ = WifiMode::Connecting;
    session_started_ms_ = now_ms;
    provisioning_connecting_ = true;
    provisioning_snapshot_.status = WifiProvisioningStatus::Connecting;
    setPowerSessionActive(true);
    if(radio_->connectStation(pending_credentials_.ssid,
                              pending_credentials_.password)) {
        return true;
    }
    stop();
    finishProvisioning(WifiProvisioningStatus::AuthFailed);
    return false;
}

bool WifiService::ssidFingerprint(char output[9]) const {
    RecursiveLock lock(mutex_);
    if(!output) return false;
    const char * value = provisioning_snapshot_.ssid[0]
        ? provisioning_snapshot_.ssid : ssid_;
    if(!value[0]) return false;
    uint8_t digest[32]{};
    if(mbedtls_sha256_ret(reinterpret_cast<const uint8_t *>(value),
                          strlen(value), digest, 0) != 0) {
        return false;
    }
    static const char hex[] = "0123456789abcdef";
    for(uint8_t index = 0; index < 4; ++index) {
        output[index * 2] = hex[digest[index] >> 4];
        output[index * 2 + 1] = hex[digest[index] & 0x0F];
    }
    output[8] = '\0';
    memset(digest, 0, sizeof(digest));
    return true;
}

void WifiService::finishProvisioning(WifiProvisioningStatus status) {
    provisioning_snapshot_.status = status;
    if(status != WifiProvisioningStatus::Connecting &&
       status != WifiProvisioningStatus::AwaitingUser) {
        pending_credentials_ = {};
        provisioning_connecting_ = false;
        pending_forget_ = false;
    }
}

void WifiService::setPowerSessionActive(bool active) {
    if(power_) power_->setWifiSessionActive(active);
}

void WifiService::stop() {
    active_purposes_ = 0;
    if(radio_ && mode_ != WifiMode::Off) radio_->disconnectAndPowerOff();
    mode_ = WifiMode::Off;
    setPowerSessionActive(false);
}

WifiMode WifiService::mode() const {
    RecursiveLock lock(mutex_);
    return mode_;
}

bool WifiService::provisioned() const {
    RecursiveLock lock(mutex_);
    return ssid_[0] != '\0';
}

WifiProvisioningSnapshot WifiService::provisioningSnapshot() const {
    RecursiveLock lock(mutex_);
    return provisioning_snapshot_;
}

}  // namespace firefly
