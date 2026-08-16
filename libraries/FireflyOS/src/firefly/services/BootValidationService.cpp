#include "BootValidationService.h"

namespace firefly {

BootValidationService::BootValidationService(
        BootValidationPlatform & platform)
    : platform_(platform) {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
}

bool BootValidationService::begin(uint32_t now_ms) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    snapshot_ = {};
    next_check_ = 0;
    started_ms_ = now_ms;
    const bool pending = platform_.pendingVerify();
    snapshot_.state = pending ? BootValidationState::Checking
                              : BootValidationState::Inactive;
    xSemaphoreGive(mutex_);
    return pending;
}

bool BootValidationService::submit(BootCheck check, bool passed) {
    const uint8_t index = static_cast<uint8_t>(check);
    if(index >= kCheckCount) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool accepted = snapshot_.state == BootValidationState::Checking &&
        snapshot_.results[index] == BootCheckResult::Pending;
    if(accepted) {
        snapshot_.results[index] = passed ? BootCheckResult::Passed
                                          : BootCheckResult::Failed;
    }
    xSemaphoreGive(mutex_);
    return accepted;
}

void BootValidationService::tick(uint32_t now_ms) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if(snapshot_.state != BootValidationState::Checking) {
        xSemaphoreGive(mutex_);
        return;
    }
    if(static_cast<uint32_t>(now_ms - started_ms_) >= kDeadlineMs) {
        requestRollback(BootValidationFailure::Timeout,
                        next_check_ < kCheckCount
                            ? static_cast<BootCheck>(next_check_)
                            : BootCheck::Count);
        xSemaphoreGive(mutex_);
        return;
    }
    if(next_check_ >= kCheckCount) {
        xSemaphoreGive(mutex_);
        return;
    }

    const BootCheckResult result = snapshot_.results[next_check_];
    if(result == BootCheckResult::Pending) {
        xSemaphoreGive(mutex_);
        return;
    }
    if(result == BootCheckResult::Failed) {
        requestRollback(BootValidationFailure::CheckFailed,
                        static_cast<BootCheck>(next_check_));
        xSemaphoreGive(mutex_);
        return;
    }

    ++next_check_;
    snapshot_.completed = next_check_;
    if(next_check_ == kCheckCount) {
        if(platform_.markValid()) {
            snapshot_.state = BootValidationState::Valid;
            snapshot_.failure = BootValidationFailure::None;
        } else {
            snapshot_.state = BootValidationState::Error;
            snapshot_.failure = BootValidationFailure::MarkValidApiFailed;
        }
    }
    xSemaphoreGive(mutex_);
}

BootValidationSnapshot BootValidationService::snapshot() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const BootValidationSnapshot copy = snapshot_;
    xSemaphoreGive(mutex_);
    return copy;
}

void BootValidationService::requestRollback(
        BootValidationFailure failure,
        BootCheck failed_check) {
    snapshot_.failure = failure;
    snapshot_.failed_check = failed_check;
    if(platform_.markInvalidAndRollback()) {
        snapshot_.state = BootValidationState::RollbackRequested;
    } else {
        snapshot_.state = BootValidationState::Error;
        snapshot_.failure = BootValidationFailure::RollbackApiFailed;
    }
}

}  // namespace firefly
