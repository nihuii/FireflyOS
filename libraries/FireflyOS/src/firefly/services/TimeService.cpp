#include "TimeService.h"

namespace firefly {

TimeService::TimeService(ClockDevice & clock)
    : clock_(clock) {}

bool TimeService::begin() {
    return reloadRtc().valid;
}

TimeSnapshot TimeService::now() const {
    return current_;
}

bool TimeService::setLocalTime(int64_t epoch_seconds) {
    if(!clock_.writeEpoch(epoch_seconds)) {
        return false;
    }

    current_.valid = true;
    current_.epoch_seconds = epoch_seconds;
    return true;
}

TimeSnapshot TimeService::reloadRtc() {
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

void TimeService::tick() {
    if(current_.valid) {
        ++current_.epoch_seconds;
    }
}

}  // namespace firefly
