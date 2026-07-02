#include "Qmi8658Device.h"

#include <Arduino.h>

namespace firefly {
namespace {

constexpr uint32_t kI2cTimeoutMs = 250;

}  // namespace

bool Qmi8658Device::begin() {
    if(initialized_) return true;
    if(!bus_.lock(kI2cTimeoutMs)) return false;
    const bool found = sensor_.begin(bus_.wire(), address_, -1, -1);
    const bool configured = found && configureNormal();
    bus_.unlock();
    initialized_ = configured;
    low_power_ = false;
    return initialized_;
}

bool Qmi8658Device::configureNormal() {
    if(sensor_.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                                   SensorQMI8658::ACC_ODR_125Hz,
                                   SensorQMI8658::LPF_MODE_0) != 0) {
        return false;
    }
    if(sensor_.configGyroscope(SensorQMI8658::GYR_RANGE_256DPS,
                               SensorQMI8658::GYR_ODR_112_1Hz,
                               SensorQMI8658::LPF_MODE_3) != 0) {
        return false;
    }
    return sensor_.enableAccelerometer() && sensor_.enableGyroscope();
}

bool Qmi8658Device::configureLowPower() {
    sensor_.disableGyroscope();
    if(sensor_.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                                   SensorQMI8658::ACC_ODR_LOWPOWER_21Hz,
                                   SensorQMI8658::LPF_MODE_0) != 0) {
        return false;
    }
    return sensor_.enableAccelerometer();
}

bool Qmi8658Device::setLowPower(bool enabled) {
    if(!initialized_) return false;
    if(enabled == low_power_) return true;
    if(!bus_.lock(kI2cTimeoutMs)) return false;
    const bool configured = enabled ? configureLowPower() : configureNormal();
    bus_.unlock();
    if(configured) low_power_ = enabled;
    return configured;
}

MotionSample Qmi8658Device::read() {
    MotionSample sample{};
    if(!initialized_ || !bus_.lock(50)) return sample;
    if(!sensor_.getDataReady()) {
        bus_.unlock();
        return sample;
    }

    const bool acceleration_ok =
        sensor_.getAccelerometer(sample.ax, sample.ay, sample.az);
    bool gyroscope_ok = true;
    if(!low_power_) {
        gyroscope_ok = sensor_.getGyroscope(sample.gx, sample.gy, sample.gz);
    }
    bus_.unlock();
    sample.timestamp_ms = millis();
    sample.valid = acceleration_ok && gyroscope_ok;
    return sample;
}

}  // namespace firefly
