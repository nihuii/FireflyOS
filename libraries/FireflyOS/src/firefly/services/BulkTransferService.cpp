#include "BulkTransferService.h"

#include <SD_MMC.h>
#include <WiFi.h>
#include <esp_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace firefly {
namespace {

class BulkRecursiveLock {
public:
    explicit BulkRecursiveLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        locked_ = !mutex_ ||
            xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) == pdTRUE;
    }
    ~BulkRecursiveLock() {
        if(locked_ && mutex_) xSemaphoreGiveRecursive(mutex_);
    }
private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
};

}  // namespace

bool SdBulkTransferStorage::cardAvailable() const {
    return storage_.bulkSdAvailable();
}

bool SdBulkTransferStorage::cardPresent() const {
    return storage_.sdAvailable();
}

uint64_t SdBulkTransferStorage::freeBytes() const {
    return storage_.bulkSdFreeBytes();
}

bool SdBulkTransferStorage::beginSession() {
    return storage_.beginBulkSdSession();
}

void SdBulkTransferStorage::endSession() {
    storage_.endBulkSdSession();
}

bool SdBulkTransferStorage::beginPart(const char * final_path,
                                      uint64_t declared_size) {
    if(!final_path || declared_size > BulkTransferService::kMaxFileBytes ||
       strnlen(final_path, sizeof(final_path_)) >= sizeof(final_path_)) {
        return false;
    }
    strlcpy(final_path_, final_path, sizeof(final_path_));
    const int length = snprintf(part_path_, sizeof(part_path_), "%s.part",
                                final_path_);
    if(length <= 0 || static_cast<size_t>(length) >= sizeof(part_path_)) {
        return false;
    }
    if(storage_.bulkManagedExists(part_path_)) {
        storage_.removeBulkManaged(part_path_);
    }
    part_ = storage_.openBulkManaged(part_path_, FILE_WRITE);
    return static_cast<bool>(part_);
}

bool SdBulkTransferStorage::append(const uint8_t * data, size_t size) {
    return part_ && data && size > 0 &&
        storage_.writeBulkManaged(part_, data, size) == size;
}

bool SdBulkTransferStorage::closePart() {
    if(!part_) return false;
    storage_.closeBulkManaged(part_);
    return true;
}

bool SdBulkTransferStorage::commitPart() {
    if(part_) storage_.closeBulkManaged(part_);
    if(storage_.bulkManagedExists(final_path_)) return false;
    return storage_.renameBulkManaged(part_path_, final_path_);
}

void SdBulkTransferStorage::removePart() {
    if(part_) storage_.closeBulkManaged(part_);
    if(part_path_[0] && storage_.bulkManagedExists(part_path_)) {
        storage_.removeBulkManaged(part_path_);
    }
    part_path_[0] = '\0';
    final_path_[0] = '\0';
}

BulkTransferService::BulkTransferService(
    BulkTransferStorage & storage,
    BulkTransferTransport & transport,
    PowerService & power,
    WifiService & wifi,
    RandomBytesCallback random_bytes)
    : storage_(storage),
      transport_(transport),
      power_(power),
      wifi_(wifi),
      random_bytes_(random_bytes ? random_bytes : defaultRandom) {
    mutex_ = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage_);
    mbedtls_sha256_init(&sha_);
}

BulkTransferService::~BulkTransferService() {
    if(snapshot_.state == BulkTransferState::Ready ||
       snapshot_.state == BulkTransferState::Receiving ||
       snapshot_.state == BulkTransferState::WaitingForNetwork ||
       snapshot_.state == BulkTransferState::Completed) {
        stopTransport(millis(), snapshot_.state == BulkTransferState::Receiving);
    }
    mbedtls_sha256_free(&sha_);
}

void BulkTransferService::defaultRandom(uint8_t * output, size_t length) {
    if(output && length) esp_fill_random(output, length);
}

void BulkTransferService::hexEncode(const uint8_t * input, size_t length,
                                    char * output) {
    static const char hex[] = "0123456789abcdef";
    for(size_t index = 0; index < length; ++index) {
        output[index * 2] = hex[input[index] >> 4];
        output[index * 2 + 1] = hex[input[index] & 0x0F];
    }
    output[length * 2] = '\0';
}

void BulkTransferService::recordResult(uint16_t request_id,
                                       BulkTransferState state,
                                       BulkTransferFailure failure) {
    ++result_generation_;
    if(result_generation_ == 0) ++result_generation_;
    snapshot_.result_request_id = request_id;
    snapshot_.result_state = state;
    snapshot_.result_failure = failure;
    snapshot_.result_generation = result_generation_;
}

void BulkTransferService::refreshExpiry(uint32_t now_ms) {
    const uint32_t idle_expiry = now_ms + kIdleTimeoutMs;
    snapshot_.expires_at_ms =
        static_cast<int32_t>(absolute_expires_at_ms_ - idle_expiry) < 0
            ? absolute_expires_at_ms_
            : idle_expiry;
}

bool BulkTransferService::normalizeManagedPath(const char * input,
                                               char output[192]) {
    if(!input || !output) return false;
    const char * path = input[0] == '/' ? input + 1 : input;
    const size_t length = strnlen(path, 192);
    if(length == 0 || length >= 191 ||
       strncmp(path, "FireflyOS/", 10) != 0) return false;
    const char * relative = path + 10;
    static const char * roots[] = {
        "Themes/", "Pictures/", "Music/", "Updates/"
    };
    const char * file_name = nullptr;
    for(const char * root : roots) {
        if(strncmp(relative, root, strlen(root)) == 0 &&
           relative[strlen(root)] != '\0') {
            file_name = relative + strlen(root);
            break;
        }
    }
    if(!file_name || strchr(file_name, '/') || strstr(relative, "..") ||
       strstr(relative, "//")) {
        return false;
    }
    const size_t file_name_length = strlen(file_name);
    if(file_name_length >= 5 &&
       strcmp(file_name + file_name_length - 5, ".part") == 0) return false;
    for(const char * cursor = path; *cursor; ++cursor) {
        const uint8_t value = static_cast<uint8_t>(*cursor);
        if(value < 0x20 || value == 0x7F || *cursor == '\\' ||
           *cursor == ':' || *cursor == '?' || *cursor == '*' ||
           *cursor == '%' || *cursor == '#') return false;
    }
    output[0] = '/';
    memcpy(output + 1, path, length + 1);
    return true;
}

bool BulkTransferService::startSession(const BulkTransferRequest & request,
                                       uint32_t now_ms) {
    BulkRecursiveLock lock(mutex_);
    const bool completed_cleanup_pending =
        snapshot_.state == BulkTransferState::Completed &&
        (cleanup_pending_ || storage_session_active_ ||
         snapshot_.token_hex[0] != '\0');
    if(cleanup_pending_ || snapshot_.state == BulkTransferState::Ready ||
       snapshot_.state == BulkTransferState::Receiving ||
       snapshot_.state == BulkTransferState::WaitingForNetwork ||
       completed_cleanup_pending) {
        recordResult(request.request_id, BulkTransferState::Error,
                     BulkTransferFailure::Busy);
        return false;
    }
    snapshot_ = {};
    snapshot_.request_id = request.request_id;
    auto reject = [this, &request](BulkTransferFailure reason) {
        if(storage_session_active_) {
            storage_.endSession();
            storage_session_active_ = false;
        }
        snapshot_.state = BulkTransferState::Error;
        snapshot_.failure = reason;
        memset(snapshot_.token_hex, 0, sizeof(snapshot_.token_hex));
        recordResult(request.request_id, snapshot_.state, reason);
        return false;
    };
    char normalized_path[sizeof(snapshot_.active_path)]{};
    if(request.request_id == 0 ||
       !normalizeManagedPath(request.managed_path, normalized_path)) {
        return reject(BulkTransferFailure::InvalidPath);
    }
    if(request.declared_size == 0 || request.declared_size > kMaxFileBytes) {
        return reject(BulkTransferFailure::FileTooLarge);
    }
    if(request.audio_active) {
        return reject(BulkTransferFailure::AudioBusy);
    }
    if(request.ota_active) {
        return reject(BulkTransferFailure::OtaBusy);
    }
    if(!power_.allowsWifiSession(true)) {
        return reject(BulkTransferFailure::LowPower);
    }
    if(!storage_.cardPresent()) {
        return reject(BulkTransferFailure::SdUnavailable);
    }
    if(!storage_.beginSession()) return reject(BulkTransferFailure::Busy);
    storage_session_active_ = true;
    if(!storage_.cardAvailable()) {
        return reject(BulkTransferFailure::SdUnavailable);
    }
    if(storage_.freeBytes() < request.declared_size + kSpaceReserveBytes) {
        return reject(BulkTransferFailure::InsufficientSpace);
    }
    snapshot_.declared_size = request.declared_size;
    strlcpy(snapshot_.active_path, normalized_path,
            sizeof(snapshot_.active_path));
    memcpy(expected_sha256_, request.expected_sha256,
           sizeof(expected_sha256_));
    uint8_t token[16]{};
    random_bytes_(token, sizeof(token));
    hexEncode(token, sizeof(token), snapshot_.token_hex);
    memset(token, 0, sizeof(token));
    session_started_ms_ = now_ms;
    absolute_expires_at_ms_ = now_ms + kSessionLimitMs;
    refreshExpiry(now_ms);
    last_activity_ms_ = now_ms;
    network_started_ms_ = now_ms;
    last_storage_check_ms_ = now_ms;
    shared_lan_ = request.prefer_shared_lan && wifi_.provisioned();
    snapshot_.state = BulkTransferState::WaitingForNetwork;
    snapshot_.failure = BulkTransferFailure::None;
    recordResult(request.request_id, snapshot_.state, snapshot_.failure);
    if(shared_lan_) {
        if(!wifi_.request(WifiPurpose::Transfer, now_ms)) {
            wifi_.release(WifiPurpose::Transfer, now_ms);
            shared_lan_ = false;
            if(!wifi_.beginSoftApSession(WifiPurpose::Transfer, now_ms)) {
                fail(BulkTransferFailure::NetworkUnavailable, now_ms, false);
                return false;
            }
        }
        return true;
    }
    if(!wifi_.beginSoftApSession(WifiPurpose::Transfer, now_ms)) {
        fail(BulkTransferFailure::NetworkUnavailable, now_ms, false);
        return false;
    }
    return true;
}

bool BulkTransferService::startTransport(bool shared_lan,
                                         uint32_t now_ms) {
    bool started = false;
    if(shared_lan) {
        started = transport_.startLan(*this, snapshot_.token_hex);
    } else {
        char ssid[24]{};
        char password[16]{};
        snprintf(ssid, sizeof(ssid), "Firefly-%c%c%c%c",
                 snapshot_.token_hex[0], snapshot_.token_hex[1],
                 snapshot_.token_hex[2], snapshot_.token_hex[3]);
        memcpy(password, snapshot_.token_hex + 16, 12);
        password[12] = '\0';
        started = transport_.startSoftAp(*this, ssid, password,
                                         snapshot_.token_hex);
        memset(password, 0, sizeof(password));
    }
    if(!started) return false;
    snapshot_.state = BulkTransferState::Ready;
    snapshot_.failure = BulkTransferFailure::None;
    strlcpy(snapshot_.endpoint, transport_.endpoint(),
            sizeof(snapshot_.endpoint));
    last_activity_ms_ = now_ms;
    refreshExpiry(now_ms);
    recordResult(snapshot_.request_id, snapshot_.state, snapshot_.failure);
    return true;
}

bool BulkTransferService::authorized(const char * token_hex) const {
    if(!token_hex || strnlen(token_hex, 33) != 32 ||
       snapshot_.token_hex[0] == '\0') return false;
    uint8_t difference = 0;
    for(size_t index = 0; index < 32; ++index) {
        difference |= static_cast<uint8_t>(
            token_hex[index] ^ snapshot_.token_hex[index]);
    }
    return difference == 0 && token_hex[32] == '\0';
}

bool BulkTransferService::beginFile(const char * token_hex,
                                    const char * path,
                                    uint64_t declared_size,
                                    const uint8_t expected_sha256[32],
                                    uint32_t now_ms) {
    BulkRecursiveLock lock(mutex_);
    if(snapshot_.state != BulkTransferState::Ready ||
       !authorized(token_hex)) {
        snapshot_.failure = BulkTransferFailure::Unauthorized;
        return false;
    }
    char normalized[192]{};
    if(!normalizeManagedPath(path, normalized)) {
        fail(BulkTransferFailure::InvalidPath, now_ms);
        return false;
    }
    if(strcmp(normalized, snapshot_.active_path) != 0) {
        fail(BulkTransferFailure::InvalidPath, now_ms);
        return false;
    }
    if(declared_size != snapshot_.declared_size) {
        fail(BulkTransferFailure::SizeMismatch, now_ms);
        return false;
    }
    if(!expected_sha256) {
        fail(BulkTransferFailure::HashMismatch, now_ms);
        return false;
    }
    uint8_t digest_difference = 0;
    for(size_t index = 0; index < sizeof(expected_sha256_); ++index) {
        digest_difference |= static_cast<uint8_t>(
            expected_sha256[index] ^ expected_sha256_[index]);
    }
    if(digest_difference != 0) {
        fail(BulkTransferFailure::HashMismatch, now_ms);
        return false;
    }
    if(storage_.freeBytes() < declared_size + kSpaceReserveBytes) {
        fail(BulkTransferFailure::InsufficientSpace, now_ms);
        return false;
    }
    if(!storage_.beginPart(normalized, declared_size)) {
        fail(BulkTransferFailure::WriteFailed, now_ms);
        return false;
    }
    mbedtls_sha256_starts_ret(&sha_, 0);
    sha_started_ = true;
    snapshot_.state = BulkTransferState::Receiving;
    snapshot_.failure = BulkTransferFailure::None;
    snapshot_.received_size = 0;
    last_activity_ms_ = now_ms;
    refreshExpiry(now_ms);
    return true;
}

bool BulkTransferService::writeChunk(const char * token_hex,
                                     const uint8_t * data,
                                     size_t size,
                                     uint32_t now_ms) {
    BulkRecursiveLock lock(mutex_);
    if(snapshot_.state != BulkTransferState::Receiving ||
       !authorized(token_hex) || !data || size == 0) {
        return false;
    }
    if(snapshot_.received_size > snapshot_.declared_size ||
       size > snapshot_.declared_size - snapshot_.received_size) {
        fail(BulkTransferFailure::SizeMismatch, now_ms);
        return false;
    }
    if(!power_.allowsWifiSession(true)) {
        fail(BulkTransferFailure::LowPower, now_ms);
        return false;
    }
    if(!storage_.cardAvailable()) {
        fail(BulkTransferFailure::SdUnavailable, now_ms);
        return false;
    }
    if(!storage_.append(data, size) ||
       mbedtls_sha256_update_ret(&sha_, data, size) != 0) {
        fail(BulkTransferFailure::WriteFailed, now_ms);
        return false;
    }
    snapshot_.received_size += size;
    last_activity_ms_ = now_ms;
    refreshExpiry(now_ms);
    return true;
}

bool BulkTransferService::finishFile(const char * token_hex,
                                     uint32_t now_ms) {
    BulkRecursiveLock lock(mutex_);
    if(snapshot_.state != BulkTransferState::Receiving ||
       !authorized(token_hex)) return false;
    uint8_t actual[32]{};
    const bool digest_ok = sha_started_ &&
        mbedtls_sha256_finish_ret(&sha_, actual) == 0;
    sha_started_ = false;
    if(snapshot_.received_size != snapshot_.declared_size) {
        memset(actual, 0, sizeof(actual));
        fail(BulkTransferFailure::SizeMismatch, now_ms);
        return false;
    }
    uint8_t difference = 0;
    for(size_t index = 0; index < sizeof(actual); ++index) {
        difference |= static_cast<uint8_t>(actual[index] ^
                                           expected_sha256_[index]);
    }
    memset(actual, 0, sizeof(actual));
    if(!digest_ok || difference != 0) {
        fail(BulkTransferFailure::HashMismatch, now_ms);
        return false;
    }
    if(!storage_.closePart() || !storage_.commitPart()) {
        fail(BulkTransferFailure::WriteFailed, now_ms);
        return false;
    }
    snapshot_.state = BulkTransferState::Completed;
    snapshot_.failure = BulkTransferFailure::None;
    last_activity_ms_ = now_ms;
    recordResult(snapshot_.request_id, snapshot_.state, snapshot_.failure);
    return true;
}

void BulkTransferService::stopTransport(uint32_t now_ms,
                                        bool remove_part) {
    if(remove_part) storage_.removePart();
    transport_.stop();
    wifi_.release(WifiPurpose::Transfer, now_ms);
    if(storage_session_active_) {
        storage_.endSession();
        storage_session_active_ = false;
    }
    sha_started_ = false;
    memset(expected_sha256_, 0, sizeof(expected_sha256_));
}

void BulkTransferService::fail(BulkTransferFailure reason,
                               uint32_t now_ms,
                               bool remove_part) {
    (void)now_ms;
    if(snapshot_.state == BulkTransferState::Completed ||
       snapshot_.state == BulkTransferState::Cancelled ||
       snapshot_.state == BulkTransferState::Error) return;
    const bool was_active = snapshot_.state == BulkTransferState::Ready ||
        snapshot_.state == BulkTransferState::Receiving ||
        snapshot_.state == BulkTransferState::WaitingForNetwork ||
        snapshot_.state == BulkTransferState::Completed;
    if(was_active || storage_session_active_) {
        cleanup_pending_ = true;
        cleanup_remove_part_ = cleanup_remove_part_ || remove_part;
    }
    snapshot_.state = BulkTransferState::Error;
    snapshot_.failure = reason;
    memset(snapshot_.token_hex, 0, sizeof(snapshot_.token_hex));
    recordResult(snapshot_.request_id, snapshot_.state, snapshot_.failure);
}

void BulkTransferService::cancel(BulkTransferFailure reason,
                                 uint32_t now_ms) {
    (void)now_ms;
    BulkRecursiveLock lock(mutex_);
    if(snapshot_.state == BulkTransferState::Completed) {
        cleanup_pending_ = true;
        cleanup_remove_part_ = false;
        return;
    }
    if(snapshot_.state == BulkTransferState::Cancelled ||
       snapshot_.state == BulkTransferState::Error) return;
    const bool active = snapshot_.state == BulkTransferState::Ready ||
        snapshot_.state == BulkTransferState::Receiving ||
        snapshot_.state == BulkTransferState::WaitingForNetwork;
    if(active || storage_session_active_) {
        cleanup_pending_ = true;
        cleanup_remove_part_ = true;
    }
    snapshot_.state = BulkTransferState::Cancelled;
    snapshot_.failure = reason == BulkTransferFailure::None
        ? BulkTransferFailure::Cancelled : reason;
    memset(snapshot_.token_hex, 0, sizeof(snapshot_.token_hex));
    recordResult(snapshot_.request_id, snapshot_.state, snapshot_.failure);
}

void BulkTransferService::reject(BulkTransferFailure reason,
                                 uint32_t now_ms) {
    BulkRecursiveLock lock(mutex_);
    if(snapshot_.state != BulkTransferState::Ready &&
       snapshot_.state != BulkTransferState::Receiving) return;
    fail(reason, now_ms);
}

bool BulkTransferService::cancelSession(uint16_t request_id,
                                        uint32_t now_ms) {
    BulkRecursiveLock lock(mutex_);
    if(request_id != 0 && request_id == snapshot_.request_id &&
       (snapshot_.state == BulkTransferState::Completed ||
        snapshot_.state == BulkTransferState::Cancelled ||
        snapshot_.state == BulkTransferState::Error)) {
        recordResult(request_id, snapshot_.state, snapshot_.failure);
        return false;
    }
    if(request_id == 0 || request_id != snapshot_.request_id ||
       (snapshot_.state != BulkTransferState::Ready &&
        snapshot_.state != BulkTransferState::Receiving &&
        snapshot_.state != BulkTransferState::WaitingForNetwork)) {
        recordResult(request_id, BulkTransferState::Error,
                     BulkTransferFailure::Busy);
        return false;
    }
    cancel(BulkTransferFailure::Cancelled, now_ms);
    return true;
}

BulkTransferFailure BulkTransferService::failure() const {
    BulkRecursiveLock lock(mutex_);
    return snapshot_.failure;
}

void BulkTransferService::tick(uint32_t now_ms) {
    bool poll_transport = false;
    {
        BulkRecursiveLock lock(mutex_);
        if(cleanup_pending_) {
            stopTransport(now_ms, cleanup_remove_part_);
            cleanup_pending_ = false;
            cleanup_remove_part_ = false;
            if(snapshot_.state == BulkTransferState::Completed) {
                memset(snapshot_.token_hex, 0,
                       sizeof(snapshot_.token_hex));
            }
            return;
        }
        if(snapshot_.state == BulkTransferState::Completed) {
            if(now_ms - last_activity_ms_ > 1000UL) {
                stopTransport(now_ms, false);
                memset(snapshot_.token_hex, 0,
                       sizeof(snapshot_.token_hex));
                return;
            }
            poll_transport = true;
        } else {
            if(snapshot_.state != BulkTransferState::Ready &&
               snapshot_.state != BulkTransferState::Receiving &&
               snapshot_.state != BulkTransferState::WaitingForNetwork) {
                return;
            }
            if(now_ms - session_started_ms_ >= kSessionLimitMs) {
                cancel(BulkTransferFailure::Timeout, now_ms);
                return;
            }
            if(!power_.allowsWifiSession(true)) {
                cancel(BulkTransferFailure::LowPower, now_ms);
                return;
            }
            if(now_ms - last_storage_check_ms_ >= 250UL) {
                last_storage_check_ms_ = now_ms;
                if(!storage_.cardAvailable()) {
                    cancel(BulkTransferFailure::SdUnavailable, now_ms);
                    return;
                }
            }
            if(now_ms - last_activity_ms_ > kIdleTimeoutMs) {
                cancel(BulkTransferFailure::Timeout, now_ms);
                return;
            }
            if(snapshot_.state == BulkTransferState::WaitingForNetwork) {
                if(shared_lan_ && wifi_.mode() == WifiMode::Connected) {
                    if(!startTransport(true, now_ms)) {
                        fail(BulkTransferFailure::NetworkUnavailable,
                             now_ms, false);
                    }
                } else if(shared_lan_ &&
                          (!wifi_.active(WifiPurpose::Transfer) ||
                           now_ms - network_started_ms_ >= kNetworkTimeoutMs)) {
                    wifi_.release(WifiPurpose::Transfer, now_ms);
                    shared_lan_ = false;
                    if(!wifi_.beginSoftApSession(WifiPurpose::Transfer,
                                                 now_ms) ||
                       !startTransport(false, now_ms)) {
                        fail(BulkTransferFailure::NetworkUnavailable,
                             now_ms, false);
                    }
                } else if(!shared_lan_ &&
                          (!wifi_.active(WifiPurpose::Transfer) ||
                           wifi_.mode() != WifiMode::SoftAp ||
                           !startTransport(false, now_ms))) {
                    fail(BulkTransferFailure::NetworkUnavailable,
                         now_ms, false);
                }
            } else {
                if(!wifi_.active(WifiPurpose::Transfer)) {
                    fail(BulkTransferFailure::NetworkUnavailable, now_ms);
                    return;
                }
                poll_transport = true;
            }
        }
    }
    if(poll_transport) transport_.poll(now_ms);
    {
        BulkRecursiveLock lock(mutex_);
        if(cleanup_pending_) {
            stopTransport(now_ms, cleanup_remove_part_);
            cleanup_pending_ = false;
            cleanup_remove_part_ = false;
            if(snapshot_.state == BulkTransferState::Completed) {
                memset(snapshot_.token_hex, 0,
                       sizeof(snapshot_.token_hex));
            }
        }
    }
}

BulkTransferSnapshot BulkTransferService::snapshot() const {
    BulkRecursiveLock lock(mutex_);
    return snapshot_;
}

EspHttpBulkTransferTransport::EspHttpBulkTransferTransport() = default;

void EspHttpBulkTransferTransport::configureRoutes() {
    if(routes_configured_) return;
    static const char * headers[] = {
        "Authorization", "X-Firefly-Path", "X-Firefly-Size",
        "X-Firefly-SHA256"
    };
    server_.collectHeaders(headers, 4);
    server_.on(
        "/upload", HTTP_POST,
        [this]() { handleComplete(); },
        [this]() { handleUpload(); });
    server_.onNotFound([this]() {
        server_.send(404, "application/json", "{\"error\":\"not_found\"}");
    });
    routes_configured_ = true;
}

bool EspHttpBulkTransferTransport::startLan(BulkTransferSink & sink,
                                            const char * token_hex) {
    if(!token_hex || WiFi.status() != WL_CONNECTED) return false;
    configureRoutes();
    sink_ = &sink;
    strlcpy(token_hex_, token_hex, sizeof(token_hex_));
    const String ip = WiFi.localIP().toString();
    snprintf(endpoint_, sizeof(endpoint_), "http://%s/upload", ip.c_str());
    soft_ap_ = false;
    server_.begin();
    return true;
}

bool EspHttpBulkTransferTransport::startSoftAp(BulkTransferSink & sink,
                                               const char * ssid,
                                               const char * password,
                                               const char * token_hex) {
    if(!ssid || !password || !token_hex) return false;
    configureRoutes();
    WiFi.mode(WIFI_AP);
    if(!WiFi.softAP(ssid, password, 1, false, 1)) return false;
    sink_ = &sink;
    strlcpy(token_hex_, token_hex, sizeof(token_hex_));
    const String ip = WiFi.softAPIP().toString();
    snprintf(endpoint_, sizeof(endpoint_), "http://%s/upload", ip.c_str());
    soft_ap_ = true;
    server_.begin();
    return true;
}

void EspHttpBulkTransferTransport::poll(uint32_t) {
    if(sink_) server_.handleClient();
}

void EspHttpBulkTransferTransport::stop() {
    server_.stop();
    if(soft_ap_) WiFi.softAPdisconnect(true);
    sink_ = nullptr;
    soft_ap_ = false;
    upload_started_ = false;
    upload_ok_ = false;
    memset(token_hex_, 0, sizeof(token_hex_));
    endpoint_[0] = '\0';
}

bool EspHttpBulkTransferTransport::requestAuthorized() {
    if(!server_.hasHeader("Authorization")) return false;
    const String expected = String("Bearer ") + token_hex_;
    const String actual = server_.header("Authorization");
    if(actual.length() != expected.length()) return false;
    uint8_t difference = 0;
    for(size_t index = 0; index < actual.length(); ++index) {
        difference |= static_cast<uint8_t>(actual[index] ^ expected[index]);
    }
    return difference == 0;
}

bool EspHttpBulkTransferTransport::parseSha256(const String & text,
                                               uint8_t output[32]) {
    if(text.length() != 64 || !output) return false;
    auto nibble = [](char value) -> int8_t {
        if(value >= '0' && value <= '9') return value - '0';
        if(value >= 'a' && value <= 'f') return value - 'a' + 10;
        if(value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    for(uint8_t index = 0; index < 32; ++index) {
        const int8_t high = nibble(text[index * 2]);
        const int8_t low = nibble(text[index * 2 + 1]);
        if(high < 0 || low < 0) return false;
        output[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

void EspHttpBulkTransferTransport::handleUpload() {
    if(!sink_) return;
    HTTPUpload & upload = server_.upload();
    const uint32_t now_ms = millis();
    if(upload.status == UPLOAD_FILE_START) {
        upload_started_ = false;
        upload_ok_ = false;
        response_code_ = 400;
        if(!requestAuthorized()) {
            response_code_ = 401;
            server_.client().stop();
            return;
        }
        if(!server_.hasHeader("X-Firefly-Path") ||
           !server_.hasHeader("X-Firefly-Size") ||
           !server_.hasHeader("X-Firefly-SHA256")) {
            const BulkTransferFailure reason =
                !server_.hasHeader("X-Firefly-Path")
                    ? BulkTransferFailure::InvalidPath
                    : (!server_.hasHeader("X-Firefly-Size")
                        ? BulkTransferFailure::SizeMismatch
                        : BulkTransferFailure::HashMismatch);
            sink_->reject(reason, now_ms);
            response_code_ = 422;
            server_.client().stop();
            return;
        }
        const String size_text = server_.header("X-Firefly-Size");
        bool size_digits = size_text.length() > 0;
        for(size_t index = 0; index < size_text.length(); ++index) {
            size_digits = size_digits && size_text[index] >= '0' &&
                size_text[index] <= '9';
        }
        char * end = nullptr;
        const uint64_t declared_size = size_digits
            ? strtoull(size_text.c_str(), &end, 10) : 0;
        uint8_t digest[32]{};
        if(!size_digits || !end || *end != '\0' || declared_size == 0 ||
           declared_size > BulkTransferService::kMaxFileBytes) {
            sink_->reject(BulkTransferFailure::SizeMismatch, now_ms);
            response_code_ = 422;
            server_.client().stop();
            return;
        }
        if(!parseSha256(server_.header("X-Firefly-SHA256"), digest)) {
            sink_->reject(BulkTransferFailure::HashMismatch, now_ms);
            response_code_ = 422;
            server_.client().stop();
            return;
        }
        upload_started_ = sink_->beginFile(
            token_hex_, server_.header("X-Firefly-Path").c_str(),
            declared_size, digest, now_ms);
        memset(digest, 0, sizeof(digest));
        response_code_ = upload_started_ ? 200 : 422;
        if(!upload_started_) server_.client().stop();
    } else if(upload.status == UPLOAD_FILE_WRITE) {
        if(upload_started_ && !sink_->writeChunk(
               token_hex_, upload.buf, upload.currentSize, now_ms)) {
            if(sink_->failure() == BulkTransferFailure::None) {
                sink_->cancel(BulkTransferFailure::WriteFailed, now_ms);
            }
            upload_started_ = false;
            response_code_ = 422;
            server_.client().stop();
        }
    } else if(upload.status == UPLOAD_FILE_END) {
        upload_ok_ = upload_started_ && sink_->finishFile(token_hex_, now_ms);
        response_code_ = upload_ok_ ? 201 : 422;
        upload_started_ = false;
    } else if(upload.status == UPLOAD_FILE_ABORTED) {
        if(upload_started_) sink_->cancel(BulkTransferFailure::Disconnected,
                                          now_ms);
        upload_started_ = false;
        response_code_ = 499;
    }
}

void EspHttpBulkTransferTransport::handleComplete() {
    if(response_code_ == 201 && upload_ok_) {
        server_.send(201, "application/json", "{\"status\":\"stored\"}");
    } else {
        server_.send(response_code_, "application/json",
                     "{\"error\":\"upload_rejected\"}");
    }
    upload_ok_ = false;
    response_code_ = 500;
}

}  // namespace firefly
