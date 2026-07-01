#include "LockedRegisterDevice.h"

namespace firefly {
namespace {

class I2cLockGuard {
public:
    I2cLockGuard(I2cBusManager & bus, uint32_t timeout_ms)
        : bus_(bus), locked_(bus_.lock(timeout_ms)) {}

    ~I2cLockGuard() {
        if(locked_) {
            bus_.unlock();
        }
    }

    bool locked() const { return locked_; }

private:
    I2cBusManager & bus_;
    bool locked_;
};

}  // namespace

bool LockedRegisterDevice::readRegister(uint8_t reg, uint8_t & value) {
    return readRegisters(reg, &value, 1);
}

bool LockedRegisterDevice::readRegisters(uint8_t reg,
                                         uint8_t * values,
                                         size_t length) {
    if(!values || length == 0 || length > kMaxPayload) {
        return false;
    }

    I2cLockGuard guard(bus_, timeout_ms_);
    if(!guard.locked()) {
        return false;
    }

    TwoWire & wire = bus_.wire();
    wire.beginTransmission(address_);
    if(wire.write(reg) != 1 || wire.endTransmission(false) != 0) {
        return false;
    }
    const size_t received = wire.requestFrom(static_cast<int>(address_),
                                             static_cast<int>(length),
                                             static_cast<int>(true));
    if(received != length) {
        return false;
    }
    for(size_t i = 0; i < length; ++i) {
        if(!wire.available()) {
            return false;
        }
        values[i] = static_cast<uint8_t>(wire.read());
    }
    return true;
}

bool LockedRegisterDevice::writeRegister(uint8_t reg, uint8_t value) {
    return writeRegisters(reg, &value, 1);
}

bool LockedRegisterDevice::writeRegisters(uint8_t reg,
                                          const uint8_t * values,
                                          size_t length) {
    if(!values || length == 0 || length > kMaxPayload) {
        return false;
    }

    I2cLockGuard guard(bus_, timeout_ms_);
    if(!guard.locked()) {
        return false;
    }

    TwoWire & wire = bus_.wire();
    wire.beginTransmission(address_);
    if(wire.write(reg) != 1 || wire.write(values, length) != length) {
        wire.endTransmission(true);
        return false;
    }
    return wire.endTransmission(true) == 0;
}

}  // namespace firefly
