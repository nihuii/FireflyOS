#pragma once

#include <stdint.h>

namespace firefly {

struct BatteryState {
    int16_t percent = -1;
    int16_t temperature_c = 0;
    uint16_t battery_mv = 0;
    bool charging = false;
    bool vbus_present = false;
    bool valid = false;
};

struct TimeState {
    int64_t epoch_seconds = 0;
    bool valid = false;
};

struct SystemState {
    BatteryState battery{};
    TimeState time{};
    bool sleeping = false;
    bool screen_off = false;
    bool phone_connected = false;
};

}  // namespace firefly
