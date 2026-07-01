#pragma once

#include <freertos/FreeRTOS.h>
#include <stdint.h>

namespace firefly {

enum class ResourceKind : uint8_t {
    AudioPlayback,
    AudioRecording,
    WifiTransfer,
    Ota,
    SdWrite,
    HighRateMotion,
    Count
};

class ResourceGovernor {
public:
    bool acquire(ResourceKind resource);
    void release(ResourceKind resource);
    bool held(ResourceKind resource) const;
    void setConstrained(bool constrained);
    bool constrained() const;

private:
    static uint16_t resourceBit(ResourceKind resource);
    static uint16_t conflictingMask(ResourceKind resource);

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    uint16_t held_mask_ = 0;
    bool constrained_ = false;
};

}  // namespace firefly
