#pragma once

#include <stdint.h>

#include "LockedRegisterDevice.h"

namespace firefly {

class Es8311Device {
public:
    explicit Es8311Device(I2cBusManager & bus,
                          uint8_t address = 0x18)
        : control_(bus, address) {}

    bool begin(uint32_t sample_rate = 16000);
    bool configureSampleRate(uint32_t sample_rate);
    bool setVolume(uint8_t percent);
    bool mute(bool enabled);
    bool sleep();
    bool initialized() const { return initialized_; }
    uint32_t sampleRate() const { return sample_rate_; }

    static bool supportsSampleRate(uint32_t sample_rate);

private:
    bool probe();
    bool write(uint8_t reg, uint8_t value);
    bool update(uint8_t reg, uint8_t clear_mask, uint8_t set_mask);

    Es8311ControlAdapter control_;
    bool initialized_ = false;
    uint32_t sample_rate_ = 0;
};

}  // namespace firefly
