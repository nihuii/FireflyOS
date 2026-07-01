#include "AppRegistry.h"

#include <string.h>

namespace firefly {

bool AppRegistry::add(const AppDescriptor & descriptor) {
    if(!descriptor.id || descriptor.id[0] == '\0' || count_ >= kMaxApps) {
        return false;
    }
    if(find(descriptor.id)) {
        return false;
    }
    apps_[count_++] = descriptor;
    return true;
}

const AppDescriptor * AppRegistry::find(const char * id) const {
    if(!id || id[0] == '\0') {
        return nullptr;
    }
    for(uint8_t i = 0; i < count_; ++i) {
        if(strcmp(apps_[i].id, id) == 0) {
            return &apps_[i];
        }
    }
    return nullptr;
}

bool AppRegistry::available(const AppDescriptor & descriptor,
                            const CapabilityRegistry & capabilities) const {
    const uint16_t available_mask = capabilities.snapshotMask();
    return (available_mask & descriptor.required_capabilities) ==
           descriptor.required_capabilities;
}

}  // namespace firefly
