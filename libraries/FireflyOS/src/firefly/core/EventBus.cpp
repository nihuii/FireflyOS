#include "EventBus.h"

namespace firefly {

bool EventBus::post(const SystemEvent & event) {
    portENTER_CRITICAL(&mux_);
    if(count_ == kCapacity) {
        if(event.priority != EventPriority::Critical) {
            portEXIT_CRITICAL(&mux_);
            return false;
        }

        uint8_t index = head_;
        for(uint8_t i = 0; i < count_; ++i) {
            if(events_[index].priority == EventPriority::Refresh) {
                events_[index] = event;
                portEXIT_CRITICAL(&mux_);
                return true;
            }
            index = static_cast<uint8_t>((index + 1U) % kCapacity);
        }
        portEXIT_CRITICAL(&mux_);
        return false;
    }

    events_[tail_] = event;
    tail_ = static_cast<uint8_t>((tail_ + 1U) % kCapacity);
    ++count_;
    portEXIT_CRITICAL(&mux_);
    return true;
}

bool EventBus::take(SystemEvent & event) {
    portENTER_CRITICAL(&mux_);
    if(count_ == 0) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }

    event = events_[head_];
    head_ = static_cast<uint8_t>((head_ + 1U) % kCapacity);
    --count_;
    portEXIT_CRITICAL(&mux_);
    return true;
}

uint8_t EventBus::size() const {
    portENTER_CRITICAL(&mux_);
    const uint8_t result = count_;
    portEXIT_CRITICAL(&mux_);
    return result;
}

}  // namespace firefly
