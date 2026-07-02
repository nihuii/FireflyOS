#include "InputService.h"

namespace firefly {

ButtonAction DebouncedButton::update(bool pressed, uint32_t now_ms) {
    if(pressed != sampled_pressed_) {
        sampled_pressed_ = pressed;
        changed_at_ms_ = now_ms;
    }

    if(sampled_pressed_ != stable_pressed_ &&
       now_ms - changed_at_ms_ >= kDebounceMs) {
        stable_pressed_ = sampled_pressed_;
        if(stable_pressed_) {
            pressed_at_ms_ = now_ms;
            long_sent_ = false;
        } else {
            const bool emit_short = !long_sent_;
            long_sent_ = false;
            return emit_short ? ButtonAction::ShortPress : ButtonAction::None;
        }
    }

    if(stable_pressed_ && !long_sent_ &&
       now_ms - pressed_at_ms_ >= kLongPressMs) {
        long_sent_ = true;
        return ButtonAction::LongPress;
    }
    return ButtonAction::None;
}

}  // namespace firefly
