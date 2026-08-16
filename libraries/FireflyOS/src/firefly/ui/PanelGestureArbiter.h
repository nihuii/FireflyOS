#pragma once

#include <stdint.h>

namespace firefly {

enum class PanelGestureDecision : uint8_t {
    Pending,
    Ignore,
    Dismiss
};

class PanelGestureArbiter {
public:
    static constexpr int16_t kDirectionThreshold = 16;

    void begin(int16_t x, int16_t y);
    PanelGestureDecision update(int16_t x, int16_t y);
    void reset();
    bool active() const { return active_; }

private:
    int16_t start_x_ = 0;
    int16_t start_y_ = 0;
    bool active_ = false;
    PanelGestureDecision decision_ = PanelGestureDecision::Pending;
};

}  // namespace firefly
