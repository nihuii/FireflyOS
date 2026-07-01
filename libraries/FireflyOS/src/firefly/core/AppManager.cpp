#include "AppManager.h"

namespace firefly {

bool AppManager::requestOpen(const char * app_id, uint32_t timestamp_ms) {
    const AppDescriptor * descriptor = registry_.find(app_id);
    if(!descriptor || !registry_.available(*descriptor, capabilities_)) {
        return false;
    }

    uint32_t app_index = 0;
    for(uint8_t i = 0; i < registry_.count(); ++i) {
        if(&registry_.at(i) == descriptor) {
            app_index = i;
            break;
        }
    }
    return events_.post(SystemEvent(EventType::AppOpenRequested,
                                    app_index,
                                    timestamp_ms));
}

bool AppManager::requestClose(uint32_t timestamp_ms) {
    if(!current_app_id_) {
        return false;
    }
    return events_.post(SystemEvent(EventType::AppCloseRequested, 0, timestamp_ms));
}

bool AppManager::confirmOpened(const char * app_id) {
    const AppDescriptor * descriptor = registry_.find(app_id);
    if(!descriptor || !registry_.available(*descriptor, capabilities_)) {
        return false;
    }
    previous_app_id_ = current_app_id_;
    current_app_id_ = descriptor->id;
    page_created_ = true;
    return true;
}

}  // namespace firefly
