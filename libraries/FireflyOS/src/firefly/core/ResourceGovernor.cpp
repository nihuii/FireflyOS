#include "ResourceGovernor.h"

namespace firefly {

uint16_t ResourceGovernor::resourceBit(ResourceKind resource) {
    const uint8_t index = static_cast<uint8_t>(resource);
    return index < static_cast<uint8_t>(ResourceKind::Count)
        ? static_cast<uint16_t>(1U << index)
        : 0;
}

uint16_t ResourceGovernor::conflictingMask(ResourceKind resource) {
    const uint16_t playback = resourceBit(ResourceKind::AudioPlayback);
    const uint16_t recording = resourceBit(ResourceKind::AudioRecording);
    const uint16_t transfer = resourceBit(ResourceKind::WifiTransfer);
    const uint16_t ota = resourceBit(ResourceKind::Ota);
    const uint16_t sd_write = resourceBit(ResourceKind::SdWrite);
    switch(resource) {
        case ResourceKind::AudioPlayback:
            return static_cast<uint16_t>(recording | ota);
        case ResourceKind::AudioRecording:
            return static_cast<uint16_t>(playback | ota);
        case ResourceKind::WifiTransfer:
            return ota;
        case ResourceKind::Ota:
            return static_cast<uint16_t>(playback | recording | transfer | sd_write);
        case ResourceKind::SdWrite:
            return ota;
        case ResourceKind::HighRateMotion:
        case ResourceKind::Count:
            return 0;
    }
    return 0;
}

bool ResourceGovernor::acquire(ResourceKind resource) {
    const uint16_t bit = resourceBit(resource);
    if(bit == 0) {
        return false;
    }
    portENTER_CRITICAL(&mux_);
    const bool denied_by_power = constrained_ &&
        resource == ResourceKind::HighRateMotion;
    const bool denied_by_conflict = (held_mask_ & conflictingMask(resource)) != 0;
    const bool allowed = !denied_by_power && !denied_by_conflict;
    if(allowed) {
        held_mask_ |= bit;
    }
    portEXIT_CRITICAL(&mux_);
    return allowed;
}

void ResourceGovernor::release(ResourceKind resource) {
    const uint16_t bit = resourceBit(resource);
    portENTER_CRITICAL(&mux_);
    held_mask_ &= static_cast<uint16_t>(~bit);
    portEXIT_CRITICAL(&mux_);
}

bool ResourceGovernor::held(ResourceKind resource) const {
    const uint16_t bit = resourceBit(resource);
    portENTER_CRITICAL(&mux_);
    const bool result = bit != 0 && (held_mask_ & bit) != 0;
    portEXIT_CRITICAL(&mux_);
    return result;
}

void ResourceGovernor::setConstrained(bool constrained) {
    portENTER_CRITICAL(&mux_);
    constrained_ = constrained;
    portEXIT_CRITICAL(&mux_);
}

bool ResourceGovernor::constrained() const {
    portENTER_CRITICAL(&mux_);
    const bool result = constrained_;
    portEXIT_CRITICAL(&mux_);
    return result;
}

}  // namespace firefly
