#include "CapabilityRegistry.h"

namespace firefly {

void CapabilityRegistry::set(Capability capability, bool available) {
    const uint16_t bit = capabilityBit(capability);
    if(bit == 0) {
        return;
    }
    portENTER_CRITICAL(&mux_);
    if(available) {
        mask_ |= bit;
    } else {
        mask_ &= static_cast<uint16_t>(~bit);
    }
    portEXIT_CRITICAL(&mux_);
}

bool CapabilityRegistry::has(Capability capability) const {
    const uint16_t bit = capabilityBit(capability);
    portENTER_CRITICAL(&mux_);
    const bool result = bit != 0 && (mask_ & bit) != 0;
    portEXIT_CRITICAL(&mux_);
    return result;
}

uint16_t CapabilityRegistry::snapshotMask() const {
    portENTER_CRITICAL(&mux_);
    const uint16_t result = mask_;
    portEXIT_CRITICAL(&mux_);
    return result;
}

}  // namespace firefly
