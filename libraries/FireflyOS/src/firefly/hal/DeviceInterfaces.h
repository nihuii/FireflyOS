#pragma once

#include <stdint.h>

#include "../core/SystemState.h"

namespace firefly {

enum class PowerButtonEvent : uint8_t {
    None,
    ShortPress,
    LongPress
};

struct MotionSample {
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    float gx = 0.0f;
    float gy = 0.0f;
    float gz = 0.0f;
    uint32_t timestamp_ms = 0;
    bool valid = false;
};

class ClockDevice {
public:
    virtual ~ClockDevice() = default;
    virtual bool readEpoch(int64_t & epoch_seconds) = 0;
    virtual bool writeEpoch(int64_t epoch_seconds) = 0;
};

class PowerDevice {
public:
    virtual ~PowerDevice() = default;
    virtual BatteryState readBattery() = 0;
    virtual void setDisplayBrightness(uint8_t value) = 0;
    virtual PowerButtonEvent readPowerButtonEvent() = 0;
    virtual void shutdown() = 0;
};

class ButtonDevice {
public:
    virtual ~ButtonDevice() = default;
    virtual bool bootPressed() const = 0;
    virtual bool powerPressed() const = 0;
};

class MotionDevice {
public:
    virtual ~MotionDevice() = default;
    virtual bool begin() = 0;
    virtual bool setLowPower(bool enabled) = 0;
    virtual MotionSample read() = 0;
};

}  // namespace firefly
