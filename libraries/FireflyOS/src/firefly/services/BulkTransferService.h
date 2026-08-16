#pragma once

#include <FS.h>
#include <WebServer.h>
#include <mbedtls/sha256.h>
#include <stddef.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "PowerService.h"
#include "StorageService.h"
#include "WifiService.h"

namespace firefly {

enum class BulkTransferState : uint8_t {
    Idle,
    WaitingForNetwork,
    Ready,
    Receiving,
    Completed,
    Cancelled,
    Error
};

enum class BulkTransferFailure : uint8_t {
    None,
    Busy,
    LowPower,
    SdUnavailable,
    InsufficientSpace,
    AudioBusy,
    OtaBusy,
    NetworkUnavailable,
    Unauthorized,
    InvalidPath,
    FileTooLarge,
    WriteFailed,
    SizeMismatch,
    HashMismatch,
    Timeout,
    Disconnected,
    Cancelled
};

struct BulkTransferRequest {
    uint16_t request_id = 0;
    bool prefer_shared_lan = true;
    bool audio_active = false;
    bool ota_active = false;
    uint64_t declared_size = 0;
    uint8_t expected_sha256[32]{};
    char managed_path[192]{};
};

struct BulkTransferSnapshot {
    BulkTransferState state = BulkTransferState::Idle;
    BulkTransferFailure failure = BulkTransferFailure::None;
    uint16_t request_id = 0;
    uint16_t result_request_id = 0;
    BulkTransferState result_state = BulkTransferState::Idle;
    BulkTransferFailure result_failure = BulkTransferFailure::None;
    uint32_t result_generation = 0;
    char token_hex[33]{};
    char endpoint[96]{};
    char active_path[192]{};
    uint64_t declared_size = 0;
    uint64_t received_size = 0;
    uint32_t expires_at_ms = 0;
};

class BulkTransferStorage {
public:
    virtual ~BulkTransferStorage() = default;
    virtual bool beginSession() { return true; }
    virtual void endSession() {}
    virtual bool cardPresent() const = 0;
    virtual bool cardAvailable() const = 0;
    virtual uint64_t freeBytes() const = 0;
    virtual bool beginPart(const char * final_path,
                           uint64_t declared_size) = 0;
    virtual bool append(const uint8_t * data, size_t size) = 0;
    virtual bool closePart() = 0;
    virtual bool commitPart() = 0;
    virtual void removePart() = 0;
};

class BulkTransferSink {
public:
    virtual ~BulkTransferSink() = default;
    virtual bool beginFile(const char * token_hex,
                           const char * path,
                           uint64_t declared_size,
                           const uint8_t expected_sha256[32],
                           uint32_t now_ms) = 0;
    virtual bool writeChunk(const char * token_hex,
                            const uint8_t * data,
                            size_t size,
                            uint32_t now_ms) = 0;
    virtual bool finishFile(const char * token_hex,
                            uint32_t now_ms) = 0;
    virtual void cancel(BulkTransferFailure reason,
                        uint32_t now_ms) = 0;
    virtual void reject(BulkTransferFailure reason,
                        uint32_t now_ms) = 0;
    virtual BulkTransferFailure failure() const = 0;
};

class BulkTransferTransport {
public:
    virtual ~BulkTransferTransport() = default;
    virtual bool startLan(BulkTransferSink & sink,
                          const char * token_hex) = 0;
    virtual bool startSoftAp(BulkTransferSink & sink,
                             const char * ssid,
                             const char * password,
                             const char * token_hex) = 0;
    virtual void poll(uint32_t now_ms) = 0;
    virtual void stop() = 0;
    virtual const char * endpoint() const = 0;
};

class SdBulkTransferStorage : public BulkTransferStorage {
public:
    explicit SdBulkTransferStorage(StorageService & storage)
        : storage_(storage) {}
    bool beginSession() override;
    void endSession() override;
    bool cardPresent() const override;
    bool cardAvailable() const override;
    uint64_t freeBytes() const override;
    bool beginPart(const char * final_path,
                   uint64_t declared_size) override;
    bool append(const uint8_t * data, size_t size) override;
    bool closePart() override;
    bool commitPart() override;
    void removePart() override;

private:
    StorageService & storage_;
    File part_{};
    char final_path_[192]{};
    char part_path_[198]{};
};

class BulkTransferService : public BulkTransferSink {
public:
    using RandomBytesCallback = void (*)(uint8_t * output, size_t length);

    static constexpr uint64_t kMaxFileBytes = 64ULL * 1024ULL * 1024ULL;
    static constexpr uint32_t kIdleTimeoutMs = 5UL * 60UL * 1000UL;
    static constexpr uint32_t kNetworkTimeoutMs = 15000;
    static constexpr uint32_t kSessionLimitMs =
        WifiService::kLongSessionLimitMs;
    static constexpr uint64_t kSpaceReserveBytes = 4096;

    BulkTransferService(BulkTransferStorage & storage,
                        BulkTransferTransport & transport,
                        PowerService & power,
                        WifiService & wifi,
                        RandomBytesCallback random_bytes = nullptr);
    ~BulkTransferService();

    bool startSession(const BulkTransferRequest & request, uint32_t now_ms);
    void tick(uint32_t now_ms);
    bool beginFile(const char * token_hex,
                   const char * path,
                   uint64_t declared_size,
                   const uint8_t expected_sha256[32],
                   uint32_t now_ms) override;
    bool writeChunk(const char * token_hex,
                    const uint8_t * data,
                    size_t size,
                    uint32_t now_ms) override;
    bool finishFile(const char * token_hex,
                    uint32_t now_ms) override;
    void cancel(BulkTransferFailure reason, uint32_t now_ms) override;
    void reject(BulkTransferFailure reason, uint32_t now_ms) override;
    bool cancelSession(uint16_t request_id, uint32_t now_ms);
    BulkTransferFailure failure() const override;
    BulkTransferSnapshot snapshot() const;

    static bool normalizeManagedPath(const char * input,
                                     char output[192]);

private:
    static void defaultRandom(uint8_t * output, size_t length);
    static void hexEncode(const uint8_t * input, size_t length,
                          char * output);
    bool authorized(const char * token_hex) const;
    bool startTransport(bool shared_lan, uint32_t now_ms);
    void stopTransport(uint32_t now_ms, bool remove_part);
    void fail(BulkTransferFailure reason, uint32_t now_ms,
              bool remove_part = true);
    void recordResult(uint16_t request_id,
                      BulkTransferState state,
                      BulkTransferFailure failure);
    void refreshExpiry(uint32_t now_ms);

    BulkTransferStorage & storage_;
    BulkTransferTransport & transport_;
    PowerService & power_;
    WifiService & wifi_;
    RandomBytesCallback random_bytes_ = nullptr;
    BulkTransferSnapshot snapshot_{};
    mbedtls_sha256_context sha_{};
    uint8_t expected_sha256_[32]{};
    uint32_t last_activity_ms_ = 0;
    uint32_t session_started_ms_ = 0;
    uint32_t absolute_expires_at_ms_ = 0;
    uint32_t network_started_ms_ = 0;
    uint32_t last_storage_check_ms_ = 0;
    bool shared_lan_ = false;
    bool sha_started_ = false;
    bool storage_session_active_ = false;
    bool cleanup_pending_ = false;
    bool cleanup_remove_part_ = false;
    uint32_t result_generation_ = 0;
    mutable StaticSemaphore_t mutex_storage_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
};

class EspHttpBulkTransferTransport : public BulkTransferTransport {
public:
    EspHttpBulkTransferTransport();
    bool startLan(BulkTransferSink & sink,
                  const char * token_hex) override;
    bool startSoftAp(BulkTransferSink & sink,
                     const char * ssid,
                     const char * password,
                     const char * token_hex) override;
    void poll(uint32_t now_ms) override;
    void stop() override;
    const char * endpoint() const override { return endpoint_; }

private:
    void configureRoutes();
    void handleUpload();
    void handleComplete();
    bool requestAuthorized();
    static bool parseSha256(const String & text, uint8_t output[32]);

    WebServer server_{80};
    BulkTransferSink * sink_ = nullptr;
    char token_hex_[33]{};
    char endpoint_[96]{};
    bool routes_configured_ = false;
    bool soft_ap_ = false;
    bool upload_started_ = false;
    bool upload_ok_ = false;
    uint16_t response_code_ = 500;
};

static_assert(sizeof(BulkTransferSnapshot) <= 384,
              "bulk transfer snapshot must remain fixed and bounded");

}  // namespace firefly
