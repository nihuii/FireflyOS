#pragma once

#include <stdint.h>

#include "CapabilityRegistry.h"

namespace firefly {

struct AppDescriptor {
    const char * id;
    const char * name;
    uint16_t required_capabilities;

    AppDescriptor(const char * app_id = nullptr,
                  const char * app_name = nullptr,
                  uint16_t requirements = 0)
        : id(app_id), name(app_name), required_capabilities(requirements) {}
};

class AppRegistry {
public:
    static constexpr uint8_t kMaxApps = 16;

    bool add(const AppDescriptor & descriptor);
    const AppDescriptor * find(const char * id) const;
    bool available(const AppDescriptor & descriptor,
                   const CapabilityRegistry & capabilities) const;
    uint8_t count() const { return count_; }
    const AppDescriptor & at(uint8_t index) const { return apps_[index]; }

private:
    AppDescriptor apps_[kMaxApps]{};
    uint8_t count_ = 0;
};

}  // namespace firefly
