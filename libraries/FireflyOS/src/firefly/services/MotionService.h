#pragma once

#include <stdint.h>

#include "../hal/DeviceInterfaces.h"

namespace firefly {

class MotionService {
public:
    static constexpr uint8_t kSampleCapacity = 32;

    explicit MotionService(MotionDevice & device) : device_(device) {}

    bool begin();
    bool setLowPower(bool enabled);
    bool poll();
    bool pushSample(const MotionSample & sample);
    uint8_t sampleCount() const { return count_; }
    MotionSample sampleAt(uint8_t index) const;
    MotionSample latest() const;

private:
    MotionDevice & device_;
    MotionSample samples_[kSampleCapacity]{};
    uint8_t head_ = 0;
    uint8_t count_ = 0;
};

}  // namespace firefly
