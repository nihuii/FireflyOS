#pragma once

#include <stdint.h>

#include "AppRegistry.h"
#include "CapabilityRegistry.h"
#include "EventBus.h"

namespace firefly {

class AppManager {
public:
    AppManager(AppRegistry & registry,
               CapabilityRegistry & capabilities,
               EventBus & events)
        : registry_(registry), capabilities_(capabilities), events_(events) {}

    bool requestOpen(const char * app_id, uint32_t timestamp_ms);
    bool requestClose(uint32_t timestamp_ms);
    bool confirmOpened(const char * app_id);

    const char * currentAppId() const { return current_app_id_; }
    const char * previousAppId() const { return previous_app_id_; }
    bool hasCreatedPage() const { return page_created_; }

private:
    AppRegistry & registry_;
    CapabilityRegistry & capabilities_;
    EventBus & events_;
    const char * current_app_id_ = nullptr;
    const char * previous_app_id_ = nullptr;
    bool page_created_ = false;
};

}  // namespace firefly
