#pragma once

#include <stdint.h>

namespace firefly {

enum class ButtonAction : uint8_t {
    None,
    ShortPress,
    LongPress
};

class DebouncedButton {
public:
    static constexpr uint32_t kDebounceMs = 30;
    static constexpr uint32_t kLongPressMs = 1000;

    ButtonAction update(bool pressed, uint32_t now_ms);

private:
    bool stable_pressed_ = false;
    bool sampled_pressed_ = false;
    bool long_sent_ = false;
    uint32_t changed_at_ms_ = 0;
    uint32_t pressed_at_ms_ = 0;
};

}  // namespace firefly
