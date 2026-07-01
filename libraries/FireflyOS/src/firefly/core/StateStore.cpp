#include "StateStore.h"

namespace firefly {
namespace {

bool equalBattery(const BatteryState & left, const BatteryState & right) {
    return left.percent == right.percent &&
           left.temperature_c == right.temperature_c &&
           left.battery_mv == right.battery_mv &&
           left.system_mv == right.system_mv &&
           left.charging == right.charging &&
           left.vbus_present == right.vbus_present &&
           left.valid == right.valid;
}

bool equalTime(const TimeState & left, const TimeState & right) {
    return left.epoch_seconds == right.epoch_seconds && left.valid == right.valid;
}

}  // namespace

SystemState StateStore::snapshot() const {
    portENTER_CRITICAL(&mux_);
    const SystemState result = state_;
    portEXIT_CRITICAL(&mux_);
    return result;
}

uint32_t StateStore::revision() const {
    portENTER_CRITICAL(&mux_);
    const uint32_t result = revision_;
    portEXIT_CRITICAL(&mux_);
    return result;
}

void StateStore::setBattery(const BatteryState & value) {
    portENTER_CRITICAL(&mux_);
    if(!equalBattery(state_.battery, value)) {
        state_.battery = value;
        ++revision_;
    }
    portEXIT_CRITICAL(&mux_);
}

void StateStore::setTime(const TimeState & value) {
    portENTER_CRITICAL(&mux_);
    if(!equalTime(state_.time, value)) {
        state_.time = value;
        ++revision_;
    }
    portEXIT_CRITICAL(&mux_);
}

void StateStore::setSleepState(bool sleeping, bool screen_off) {
    portENTER_CRITICAL(&mux_);
    if(state_.sleeping != sleeping || state_.screen_off != screen_off) {
        state_.sleeping = sleeping;
        state_.screen_off = screen_off;
        ++revision_;
    }
    portEXIT_CRITICAL(&mux_);
}

}  // namespace firefly
