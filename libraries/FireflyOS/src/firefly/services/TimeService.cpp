#include "TimeService.h"

namespace firefly {
namespace {

class TimeRecursiveLock {
public:
    explicit TimeRecursiveLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        locked_ = !mutex_ ||
            xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) == pdTRUE;
    }
    ~TimeRecursiveLock() {
        if(locked_ && mutex_) xSemaphoreGiveRecursive(mutex_);
    }
private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
};

}  // namespace

TimeService::TimeService(ClockDevice & clock)
    : clock_(clock) {
    mutex_ = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage_);
}

bool TimeService::begin() {
    return reloadRtc().valid;
}

TimeSnapshot TimeService::now() const {
    TimeRecursiveLock lock(mutex_);
    return current_;
}

bool TimeService::setLocalTime(int64_t epoch_seconds) {
    TimeRecursiveLock lock(mutex_);
    if(!clock_.writeEpoch(epoch_seconds)) {
        return false;
    }

    current_.valid = true;
    current_.epoch_seconds = epoch_seconds;
    return true;
}

TimeSnapshot TimeService::reloadRtc() {
    TimeRecursiveLock lock(mutex_);
    int64_t epoch_seconds = 0;
    if(!clock_.readEpoch(epoch_seconds)) {
        current_.valid = false;
        current_.epoch_seconds = 0;
        return current_;
    }

    current_.valid = true;
    current_.epoch_seconds = epoch_seconds;
    return current_;
}

bool TimeService::applyNetworkTime(int64_t epoch_seconds,
                                   bool alarm_ringing) {
    TimeRecursiveLock lock(mutex_);
    if(epoch_seconds <= 0) return false;
    const int64_t difference = current_.valid
        ? epoch_seconds - current_.epoch_seconds : epoch_seconds;
    if(current_.valid && difference >= -2 && difference <= 2) {
        network_sync_pending_ = false;
        deferred_network_epoch_ = 0;
        return true;
    }
    if(alarm_ringing) {
        deferred_network_epoch_ = epoch_seconds;
        network_sync_pending_ = true;
        return true;
    }
    network_sync_pending_ = false;
    deferred_network_epoch_ = 0;
    return setLocalTime(epoch_seconds);
}

bool TimeService::flushDeferredNetworkTime(bool alarm_ringing) {
    TimeRecursiveLock lock(mutex_);
    if(!network_sync_pending_ || alarm_ringing) return false;
    const int64_t pending = deferred_network_epoch_;
    network_sync_pending_ = false;
    deferred_network_epoch_ = 0;
    return setLocalTime(pending);
}

bool TimeService::networkSyncPending() const {
    TimeRecursiveLock lock(mutex_);
    return network_sync_pending_;
}

void TimeService::tick() {
    TimeRecursiveLock lock(mutex_);
    if(current_.valid) {
        ++current_.epoch_seconds;
    }
}

}  // namespace firefly
