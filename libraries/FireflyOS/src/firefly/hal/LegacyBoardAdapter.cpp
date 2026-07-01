#include "LegacyBoardAdapter.h"

#include <time.h>

namespace firefly {
namespace {

constexpr uint32_t kI2cTimeoutMs = 50;

}  // namespace

bool LegacyBoardAdapter::lockBus() {
    return !i2c_ || i2c_->lock(kI2cTimeoutMs);
}

void LegacyBoardAdapter::unlockBus() {
    if(i2c_) {
        i2c_->unlock();
    }
}

bool LegacyBoardAdapter::readEpoch(int64_t & epoch_seconds) {
    if(!lockBus()) {
        return false;
    }
    const RTC_DateTime value = rtc_.getDateTime();
    unlockBus();

    if(value.getYear() < 2024 || value.getMonth() < 1 || value.getMonth() > 12 ||
       value.getDay() < 1 || value.getDay() > 31) {
        return false;
    }
    struct tm local_time{};
    local_time.tm_year = value.getYear() - 1900;
    local_time.tm_mon = value.getMonth() - 1;
    local_time.tm_mday = value.getDay();
    local_time.tm_hour = value.getHour();
    local_time.tm_min = value.getMinute();
    local_time.tm_sec = value.getSecond();
    local_time.tm_isdst = -1;
    const time_t result = mktime(&local_time);
    if(result < 0) {
        return false;
    }
    epoch_seconds = static_cast<int64_t>(result);
    return true;
}

bool LegacyBoardAdapter::writeEpoch(int64_t epoch_seconds) {
    const time_t raw_time = static_cast<time_t>(epoch_seconds);
    struct tm local_time{};
    if(!localtime_r(&raw_time, &local_time)) {
        return false;
    }
    if(!lockBus()) {
        return false;
    }
    rtc_.setDateTime(RTC_DateTime(local_time));
    unlockBus();
    return true;
}

BatteryState LegacyBoardAdapter::readBattery() {
    BatteryState state{};
    if(!lockBus()) {
        return state;
    }
    state.percent = static_cast<int16_t>(power_.getBatteryPercent());
    state.temperature_c = static_cast<int16_t>(power_.getTemperature());
    state.battery_mv = power_.getBattVoltage();
    state.system_mv = power_.getSystemVoltage();
    state.charging = power_.isCharging();
    state.vbus_present = power_.isVbusIn();
    unlockBus();
    state.valid = state.percent >= 0 || state.battery_mv > 0 || state.vbus_present;
    return state;
}

void LegacyBoardAdapter::setDisplayBrightness(uint8_t value) {
    display_.setBrightness(value);
}

}  // namespace firefly
