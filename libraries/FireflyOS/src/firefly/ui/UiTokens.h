#pragma once

#include <stdint.h>

namespace firefly {

struct UiTokens {
    uint16_t bg_base;
    uint16_t bg_surface;
    uint16_t firefly_primary;
    uint16_t firefly_secondary;
    uint16_t text_primary;
    uint16_t text_secondary;
    uint16_t sam_energy;
    uint16_t sam_ignition;
    uint16_t critical;
    uint8_t radius_card;
    uint8_t radius_button;
    uint8_t touch_min;
    uint8_t side_inset;
    uint8_t bottom_inset;
};

}  // namespace firefly
