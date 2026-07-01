#pragma once

#include <stdint.h>

namespace firefly {

enum class Route : uint8_t {
    Lock,
    Home,
    Settings,
    Clock,
    Calendar,
    Activity,
    Weather,
    Music,
    Recorder,
    Files,
    Themes,
    Tools,
    Diagnostics
};

class NavigationController {
public:
    static constexpr uint8_t kDepth = 6;

    NavigationController();
    bool open(Route route);
    Route back();
    Route current() const;
    uint8_t depth() const { return depth_; }
    void lock();

private:
    Route stack_[kDepth]{};
    uint8_t depth_ = 1;
};

}  // namespace firefly
