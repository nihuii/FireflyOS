#include "PanelGestureArbiter.h"

namespace firefly {
namespace {

int32_t magnitude(int32_t value) {
    return value < 0 ? -value : value;
}

}  // namespace

void PanelGestureArbiter::begin(int16_t x, int16_t y) {
    start_x_ = x;
    start_y_ = y;
    active_ = true;
    decision_ = PanelGestureDecision::Pending;
}

PanelGestureDecision PanelGestureArbiter::update(int16_t x, int16_t y) {
    if(!active_) {
        return PanelGestureDecision::Ignore;
    }
    if(decision_ != PanelGestureDecision::Pending) {
        return decision_;
    }

    const int32_t dx = static_cast<int32_t>(x) - start_x_;
    const int32_t dy = static_cast<int32_t>(y) - start_y_;
    const int32_t abs_dx = magnitude(dx);
    const int32_t abs_dy = magnitude(dy);
    if(abs_dx < kDirectionThreshold && abs_dy < kDirectionThreshold) {
        return PanelGestureDecision::Pending;
    }

    decision_ = dy <= -kDirectionThreshold && abs_dy > abs_dx
        ? PanelGestureDecision::Dismiss
        : PanelGestureDecision::Ignore;
    return decision_;
}

void PanelGestureArbiter::reset() {
    start_x_ = 0;
    start_y_ = 0;
    active_ = false;
    decision_ = PanelGestureDecision::Pending;
}

}  // namespace firefly
