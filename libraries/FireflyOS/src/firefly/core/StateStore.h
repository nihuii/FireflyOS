#pragma once

#include <freertos/FreeRTOS.h>

#include "SystemState.h"

namespace firefly {

class StateStore {
public:
    SystemState snapshot() const;
    uint32_t revision() const;
    void setBattery(const BatteryState & value);
    void setTime(const TimeState & value);
    void setSleepState(bool sleeping, bool screen_off);

private:
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    SystemState state_{};
    uint32_t revision_ = 0;
};

}  // namespace firefly
