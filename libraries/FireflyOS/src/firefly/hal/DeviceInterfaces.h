#pragma once

#include <stdint.h>

#include "../core/SystemState.h"

namespace firefly {

enum class PowerButtonEvent : uint8_t {
    None,
    ShortPress,
    LongPress
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

}  // namespace firefly
