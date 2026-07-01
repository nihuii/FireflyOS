#pragma once

#include <stddef.h>
#include <stdint.h>

#include "I2cBusManager.h"

namespace firefly {

class LockedRegisterDevice {
public:
    static constexpr size_t kMaxPayload = 30;

    LockedRegisterDevice(I2cBusManager & bus,
                         uint8_t address,
                         uint32_t timeout_ms = 50)
        : bus_(bus), address_(address), timeout_ms_(timeout_ms) {}

    uint8_t address() const { return address_; }
    bool readRegister(uint8_t reg, uint8_t & value);
    bool readRegisters(uint8_t reg, uint8_t * values, size_t length);
    bool writeRegister(uint8_t reg, uint8_t value);
    bool writeRegisters(uint8_t reg, const uint8_t * values, size_t length);

protected:
    I2cBusManager & bus_;
    uint8_t address_;
    uint32_t timeout_ms_;
};

class Qmi8658ControlAdapter final : public LockedRegisterDevice {
public:
    explicit Qmi8658ControlAdapter(I2cBusManager & bus,
                                   uint8_t address = 0x6B,
                                   uint32_t timeout_ms = 50)
        : LockedRegisterDevice(bus, address, timeout_ms) {}
};

class Es8311ControlAdapter final : public LockedRegisterDevice {
public:
    explicit Es8311ControlAdapter(I2cBusManager & bus,
                                  uint8_t address = 0x18,
                                  uint32_t timeout_ms = 50)
        : LockedRegisterDevice(bus, address, timeout_ms) {}
};

}  // namespace firefly
