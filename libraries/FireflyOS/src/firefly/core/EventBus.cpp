#include "EventBus.h"

namespace firefly {

bool EventBus::post(const SystemEvent & event) {
    if(count_ == kCapacity) {
        if(event.priority != EventPriority::Critical) {
            return false;
        }

        uint8_t index = head_;
        for(uint8_t i = 0; i < count_; ++i) {
            if(events_[index].priority == EventPriority::Refresh) {
                events_[index] = event;
                return true;
            }
            index = static_cast<uint8_t>((index + 1U) % kCapacity);
        }
        return false;
    }

    events_[tail_] = event;
    tail_ = static_cast<uint8_t>((tail_ + 1U) % kCapacity);
    ++count_;
    return true;
}

bool EventBus::take(SystemEvent & event) {
    if(count_ == 0) {
        return false;
    }

    event = events_[head_];
    head_ = static_cast<uint8_t>((head_ + 1U) % kCapacity);
    --count_;
    return true;
}

uint8_t EventBus::size() const {
    return count_;
}

}  // namespace firefly
