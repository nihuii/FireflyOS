#include "MotionService.h"

namespace firefly {

bool MotionService::begin() {
    return device_.begin();
}

bool MotionService::setLowPower(bool enabled) {
    return device_.setLowPower(enabled);
}

bool MotionService::poll() {
    return pushSample(device_.read());
}

bool MotionService::pushSample(const MotionSample & sample) {
    if(!sample.valid) return false;

    uint8_t index = 0;
    if(count_ < kSampleCapacity) {
        index = static_cast<uint8_t>((head_ + count_) % kSampleCapacity);
        ++count_;
    } else {
        index = head_;
        head_ = static_cast<uint8_t>((head_ + 1U) % kSampleCapacity);
    }
    samples_[index] = sample;
    return true;
}

MotionSample MotionService::sampleAt(uint8_t index) const {
    if(index >= count_) return {};
    return samples_[(head_ + index) % kSampleCapacity];
}

MotionSample MotionService::latest() const {
    return count_ == 0 ? MotionSample{} : sampleAt(count_ - 1U);
}

}  // namespace firefly
