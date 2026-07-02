#pragma once

#include <SensorQMI8658.hpp>

#include "DeviceInterfaces.h"
#include "I2cBusManager.h"

namespace firefly {

class Qmi8658Device final : public MotionDevice {
public:
    explicit Qmi8658Device(I2cBusManager & bus,
                           uint8_t address = 0x6B)
        : bus_(bus), address_(address) {}

    bool begin() override;
    bool setLowPower(bool enabled) override;
    MotionSample read() override;
    uint8_t address() const { return address_; }
    bool initialized() const { return initialized_; }

private:
    bool configureNormal();
    bool configureLowPower();

    I2cBusManager & bus_;
    uint8_t address_;
    SensorQMI8658 sensor_{};
    bool initialized_ = false;
    bool low_power_ = false;
};

}  // namespace firefly
