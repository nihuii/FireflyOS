#pragma once

#include "SystemEvent.h"

namespace firefly {

class EventBus {
public:
    static constexpr uint8_t kCapacity = 16;

    bool post(const SystemEvent & event);
    bool take(SystemEvent & event);
    uint8_t size() const;

private:
    SystemEvent events_[kCapacity]{};
    uint8_t head_ = 0;
    uint8_t tail_ = 0;
    uint8_t count_ = 0;
};

}  // namespace firefly
