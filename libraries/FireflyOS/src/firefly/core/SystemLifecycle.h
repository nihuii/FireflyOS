#pragma once

#include <freertos/FreeRTOS.h>
#include <stdint.h>

namespace firefly {

class ResourceGovernor;

enum class SystemPhase : uint8_t {
    Booting,
    Locked,
    Active,
    Glance,
    Sleeping,
    Updating,
    Error
};

class SystemLifecycle {
public:
    SystemPhase phase() const;
    bool transition(SystemPhase target);
    bool transition(SystemPhase target, const ResourceGovernor & resources);

private:
    static bool canTransition(SystemPhase from, SystemPhase to);
    bool transitionChecked(SystemPhase target, bool update_allowed);

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    SystemPhase phase_ = SystemPhase::Booting;
};

}  // namespace firefly
