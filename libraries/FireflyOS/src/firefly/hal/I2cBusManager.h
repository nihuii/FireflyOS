#pragma once

#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdint.h>

namespace firefly {

class I2cBusManager {
public:
    explicit I2cBusManager(TwoWire & wire);

    bool lock(uint32_t timeout_ms);
    void unlock();
    TwoWire & wire() { return wire_; }

    I2cBusManager(const I2cBusManager &) = delete;
    I2cBusManager & operator=(const I2cBusManager &) = delete;

private:
    TwoWire & wire_;
    StaticSemaphore_t mutex_storage_{};
    SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace firefly
