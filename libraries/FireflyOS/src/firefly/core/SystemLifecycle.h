#pragma once

#include <freertos/FreeRTOS.h>
#include <stdint.h>

namespace firefly {

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

private:
    static bool canTransition(SystemPhase from, SystemPhase to);

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    SystemPhase phase_ = SystemPhase::Booting;
};

}  // namespace firefly
