#include "SystemLifecycle.h"

namespace firefly {

SystemPhase SystemLifecycle::phase() const {
    portENTER_CRITICAL(&mux_);
    const SystemPhase result = phase_;
    portEXIT_CRITICAL(&mux_);
    return result;
}

bool SystemLifecycle::transition(SystemPhase target) {
    portENTER_CRITICAL(&mux_);
    const bool allowed = phase_ == target || canTransition(phase_, target);
    if(allowed) {
        phase_ = target;
    }
    portEXIT_CRITICAL(&mux_);
    return allowed;
}

bool SystemLifecycle::canTransition(SystemPhase from, SystemPhase to) {
    switch(from) {
        case SystemPhase::Booting:
            return to == SystemPhase::Locked || to == SystemPhase::Error;
        case SystemPhase::Locked:
            return to == SystemPhase::Active || to == SystemPhase::Glance ||
                   to == SystemPhase::Sleeping || to == SystemPhase::Error;
        case SystemPhase::Active:
            return to == SystemPhase::Locked || to == SystemPhase::Glance ||
                   to == SystemPhase::Sleeping || to == SystemPhase::Updating ||
                   to == SystemPhase::Error;
        case SystemPhase::Glance:
            return to == SystemPhase::Locked || to == SystemPhase::Active ||
                   to == SystemPhase::Sleeping || to == SystemPhase::Error;
        case SystemPhase::Sleeping:
            return to == SystemPhase::Glance || to == SystemPhase::Locked ||
                   to == SystemPhase::Error;
        case SystemPhase::Updating:
            return to == SystemPhase::Active || to == SystemPhase::Error;
        case SystemPhase::Error:
            return to == SystemPhase::Booting;
    }
    return false;
}

}  // namespace firefly
