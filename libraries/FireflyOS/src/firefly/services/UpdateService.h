#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mbedtls/sha256.h>
#include <stddef.h>
#include <stdint.h>

#include "../core/ResourceGovernor.h"
#include "UpdateManifest.h"

namespace firefly {

enum class UpdateState : uint8_t {
    Idle,
    Available,
    Blocked,
    Downloading,
    Verifying,
    Writing,
    RebootPending,
    BootChecking,
    Completed,
    Failed,
    RollbackRequested,
    RolledBack
};

enum class UpdateFailure : uint8_t {
    None,
    LowPower,
    AlarmActive,
    AudioBusy,
    TransferBusy,
    OtaBusy,
    WrongProduct,
    BuildNotNewer,
    MinBuildMismatch,
    PackageTooLarge,
    NoHttpsEndpoint,
    SourceUnavailable,
    ManifestInvalid,
    SignatureInvalid,
    ShortPackage,
    OversizedPackage,
    HashMismatch,
    WriteFailed,
    FinalizeFailed,
    Cancelled,
    BootValidationFailed,
    Timeout
};

enum class UpdateIoResult : uint8_t {
    Ok,
    End,
    Unavailable,
    Timeout,
    Error,
    NoEndpoint
};

struct UpdateRuntimeGate {
    int16_t battery_percent = -1;
    bool battery_valid = false;
    bool charging = false;
    bool alarm_active = false;
    bool music_active = false;
    bool recording_active = false;
    bool transfer_active = false;
    bool ota_active = false;
};

struct UpdateSnapshot {
    UpdateState state = UpdateState::Idle;
    UpdateFailure failure = UpdateFailure::None;
    uint32_t build = 0;
    uint32_t size = 0;
    uint32_t processed = 0;
    uint8_t progress_percent = 0;
    char version[16]{};
    bool cancel_allowed = false;
};

class UpdateSource {
public:
    virtual ~UpdateSource() = default;
    virtual UpdateIoResult open(const UpdateManifest & manifest) = 0;
    virtual UpdateIoResult read(uint8_t * output,
                                size_t capacity,
                                size_t & output_length) = 0;
    virtual void close() = 0;
};

class UpdateWriter {
public:
    virtual ~UpdateWriter() = default;
    virtual bool begin(uint32_t size) = 0;
    virtual bool write(const uint8_t * data, size_t size) = 0;
    virtual bool finish() = 0;
    virtual bool selectForNextBoot() = 0;
    virtual void abort() = 0;
};

class UpdateService {
public:
    static constexpr size_t kChunkBytes = 4096;
    static constexpr uint32_t kStallTimeoutMs = 15000;
    static constexpr uint32_t kOtaSlotBytes = 0xB00000UL;

    UpdateService(ResourceGovernor & resources,
                  UpdateWriter & writer,
                  uint32_t current_build);
    ~UpdateService();

    bool offer(const UpdateManifest & manifest,
               UpdateSource & source,
               const UpdateRuntimeGate & gate,
               uint32_t now_ms);
    bool start(const UpdateRuntimeGate & gate, uint32_t now_ms);
    void tick(uint32_t now_ms);
    bool cancel(uint32_t now_ms);
    bool reset();
    bool reportFailure(UpdateFailure failure);
    UpdateSnapshot snapshot() const;

    static UpdateFailure preflightFailure(const UpdateRuntimeGate & gate);

private:
    static bool isActive(UpdateState state);
    static bool isTerminal(UpdateState state);
    void setBlocked(UpdateFailure failure);
    void fail(UpdateFailure failure, bool abort_writer);
    void cleanup(bool abort_writer);
    void updateProgress();
    UpdateFailure ioFailure(UpdateIoResult result) const;

    ResourceGovernor & resources_;
    UpdateWriter & writer_;
    const uint32_t current_build_;
    UpdateSource * source_ = nullptr;
    UpdateManifest manifest_{};
    UpdateSnapshot snapshot_{};
    mbedtls_sha256_context sha_context_{};
    StaticSemaphore_t mutex_storage_{};
    SemaphoreHandle_t mutex_ = nullptr;
    uint32_t last_progress_ms_ = 0;
    bool resource_held_ = false;
    bool source_open_ = false;
    bool writer_started_ = false;
    bool sha_started_ = false;
};

static_assert(sizeof(UpdateSnapshot) <= 64,
              "update snapshot must remain fixed and bounded");

}  // namespace firefly
