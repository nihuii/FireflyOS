#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdint.h>

namespace firefly {

enum class BootCheck : uint8_t {
    Rtc,
    Pmu,
    Display,
    Touch,
    Nvs,
    MainUi,
    Count
};

enum class BootCheckResult : uint8_t {
    Pending,
    Passed,
    Failed
};

enum class BootValidationState : uint8_t {
    Inactive,
    Checking,
    Valid,
    RollbackRequested,
    Error
};

enum class BootValidationFailure : uint8_t {
    None,
    CheckFailed,
    Timeout,
    MarkValidApiFailed,
    RollbackApiFailed
};

struct BootValidationSnapshot {
    BootValidationState state = BootValidationState::Inactive;
    BootValidationFailure failure = BootValidationFailure::None;
    BootCheck failed_check = BootCheck::Count;
    uint8_t completed = 0;
    BootCheckResult results[static_cast<uint8_t>(BootCheck::Count)]{};
};

class BootValidationPlatform {
public:
    virtual ~BootValidationPlatform() = default;
    virtual bool pendingVerify() = 0;
    virtual bool markValid() = 0;
    virtual bool markInvalidAndRollback() = 0;
};

class BootValidationService {
public:
    static constexpr uint32_t kDeadlineMs = 30000;
    static constexpr uint8_t kCheckCount =
        static_cast<uint8_t>(BootCheck::Count);

    explicit BootValidationService(BootValidationPlatform & platform);

    bool begin(uint32_t now_ms);
    bool submit(BootCheck check, bool passed);
    void tick(uint32_t now_ms);
    BootValidationSnapshot snapshot() const;

private:
    void requestRollback(BootValidationFailure failure,
                         BootCheck failed_check);

    BootValidationPlatform & platform_;
    BootValidationSnapshot snapshot_{};
    StaticSemaphore_t mutex_storage_{};
    SemaphoreHandle_t mutex_ = nullptr;
    uint32_t started_ms_ = 0;
    uint8_t next_check_ = 0;
};

static_assert(sizeof(BootValidationSnapshot) <= 16,
              "boot validation snapshot must remain fixed and bounded");

}  // namespace firefly
