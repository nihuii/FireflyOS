#pragma once

#include <freertos/FreeRTOS.h>
#include <stdint.h>

namespace firefly {

enum class Capability : uint8_t {
    Display,
    Touch,
    Rtc,
    Power,
    PowerButton,
    Motion,
    Audio,
    Sd,
    Wifi,
    Ble,
    Count
};

constexpr uint16_t capabilityBit(Capability value) {
    return static_cast<uint8_t>(value) < static_cast<uint8_t>(Capability::Count)
        ? static_cast<uint16_t>(1U << static_cast<uint8_t>(value))
        : 0;
}

class CapabilityRegistry {
public:
    void set(Capability capability, bool available);
    bool has(Capability capability) const;
    uint16_t snapshotMask() const;

private:
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    uint16_t mask_ = 0;
};

}  // namespace firefly
