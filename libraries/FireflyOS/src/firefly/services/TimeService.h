#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "../hal/DeviceInterfaces.h"

namespace firefly {

struct TimeSnapshot {
    bool valid = false;
    int64_t epoch_seconds = 0;
};

class TimeService {
public:
    explicit TimeService(ClockDevice & clock);

    bool begin();
    TimeSnapshot now() const;
    bool setLocalTime(int64_t epoch_seconds);
    TimeSnapshot reloadRtc();
    bool applyNetworkTime(int64_t epoch_seconds, bool alarm_ringing);
    bool flushDeferredNetworkTime(bool alarm_ringing);
    bool networkSyncPending() const;
    void tick();

private:
    ClockDevice & clock_;
    TimeSnapshot current_{};
    int64_t deferred_network_epoch_ = 0;
    bool network_sync_pending_ = false;
    mutable StaticSemaphore_t mutex_storage_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace firefly
