#pragma once

#include <freertos/FreeRTOS.h>
#include <stdint.h>

namespace firefly {

enum class HardwareDevice : uint8_t {
    Rtc,
    Pmu,
    Imu,
    Sd,
    Codec,
    Ble,
    Wifi,
    Count
};

enum class HardwareAvailability : uint8_t {
    Available,
    Unavailable,
    Degraded
};

enum class HardwareFailure : uint8_t {
    None,
    NotProbed,
    NotDetected,
    InitFailed,
    IoFailure,
    Timeout,
    Busy
};

struct HardwareCapabilitySnapshot {
    HardwareAvailability status[
        static_cast<uint8_t>(HardwareDevice::Count)]{};
    HardwareFailure failure[
        static_cast<uint8_t>(HardwareDevice::Count)]{};
};

class HardwareCapabilities {
public:
    HardwareCapabilities();

    void set(HardwareDevice device,
             HardwareAvailability status,
             HardwareFailure failure);
    bool available(HardwareDevice device) const;
    HardwareAvailability status(HardwareDevice device) const;
    HardwareFailure failure(HardwareDevice device) const;
    HardwareCapabilitySnapshot snapshot() const;

    static const char * deviceName(HardwareDevice device);
    static const char * statusText(HardwareAvailability status);
    static const char * failureText(HardwareFailure failure);

private:
    static bool valid(HardwareDevice device);

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    HardwareCapabilitySnapshot snapshot_{};
};

static_assert(sizeof(HardwareCapabilitySnapshot) == 14,
              "hardware capability snapshot must stay fixed and bounded");

}  // namespace firefly
