#include "Es8311Device.h"

#include <Arduino.h>

namespace firefly {
namespace {

constexpr uint8_t kReset = 0x00;
constexpr uint8_t kClock1 = 0x01;
constexpr uint8_t kClock2 = 0x02;
constexpr uint8_t kClock3 = 0x03;
constexpr uint8_t kClock4 = 0x04;
constexpr uint8_t kClock5 = 0x05;
constexpr uint8_t kClock6 = 0x06;
constexpr uint8_t kClock7 = 0x07;
constexpr uint8_t kClock8 = 0x08;
constexpr uint8_t kSerialIn = 0x09;
constexpr uint8_t kSerialOut = 0x0A;
constexpr uint8_t kSystem0d = 0x0D;
constexpr uint8_t kSystem0e = 0x0E;
constexpr uint8_t kSystem12 = 0x12;
constexpr uint8_t kSystem13 = 0x13;
constexpr uint8_t kSystem14 = 0x14;
constexpr uint8_t kAdc16 = 0x16;
constexpr uint8_t kAdc17 = 0x17;
constexpr uint8_t kAdc1c = 0x1C;
constexpr uint8_t kDac31 = 0x31;
constexpr uint8_t kDac32 = 0x32;
constexpr uint8_t kDac37 = 0x37;
constexpr uint8_t kChipId1 = 0xFD;
constexpr uint8_t kChipId2 = 0xFE;

}  // namespace

bool Es8311Device::begin(uint32_t sample_rate) {
    initialized_ = false;
    sample_rate_ = 0;
    if(!supportsSampleRate(sample_rate) || !probe()) return false;

    if(!write(kReset, 0x1F)) return false;
    delay(1);
    if(!write(kReset, 0x00) || !write(kReset, 0x80) ||
       !write(kClock1, 0x3F) || !configureSampleRate(sample_rate) ||
       !update(kReset, 0x40, 0x00) ||
       !write(kSerialIn, 0x0C) || !write(kSerialOut, 0x0C) ||
       !write(kSystem0d, 0x01) || !write(kSystem0e, 0x02) ||
       !write(kSystem12, 0x00) || !write(kSystem13, 0x10) ||
       !write(kAdc1c, 0x6A) || !write(kDac37, 0x08) ||
       !write(kAdc17, 0xC8) || !write(kSystem14, 0x1A) ||
       !write(kAdc16, 0x03) || !setVolume(50) || !mute(true)) {
        write(kReset, 0x1F);
        return false;
    }
    initialized_ = true;
    return true;
}

bool Es8311Device::configureSampleRate(uint32_t sample_rate) {
    if(!supportsSampleRate(sample_rate)) return false;

    // FireflyOS always drives MCLK at 256 * sample rate. Every supported
    // coefficient therefore shares the same divider fields in ES8311.
    if(!update(kClock2, 0xF8, 0x00) ||
       !write(kClock3, 0x10) || !write(kClock4, 0x10) ||
       !write(kClock5, 0x00) ||
       !update(kClock6, 0x1F, 0x03) ||
       !update(kClock7, 0x3F, 0x00) ||
       !write(kClock8, 0xFF)) {
        return false;
    }
    sample_rate_ = sample_rate;
    return true;
}

bool Es8311Device::setVolume(uint8_t percent) {
    if(percent > 100) percent = 100;
    const uint8_t value = percent == 0
        ? 0
        : static_cast<uint8_t>((static_cast<uint16_t>(percent) * 256U / 100U) - 1U);
    return write(kDac32, value);
}

bool Es8311Device::mute(bool enabled) {
    return update(kDac31, 0x60, enabled ? 0x60 : 0x00);
}

bool Es8311Device::sleep() {
    const bool muted = mute(true);
    const bool reset = write(kReset, 0x1F);
    initialized_ = false;
    sample_rate_ = 0;
    return muted && reset;
}

bool Es8311Device::supportsSampleRate(uint32_t sample_rate) {
    static const uint32_t rates[] = {
        8000, 11025, 12000, 16000, 22050,
        24000, 32000, 44100, 48000,
    };
    for(uint32_t rate : rates) {
        if(sample_rate == rate) return true;
    }
    return false;
}

bool Es8311Device::probe() {
    uint8_t id1 = 0;
    uint8_t id2 = 0;
    if(!control_.readRegister(kChipId1, id1) ||
       !control_.readRegister(kChipId2, id2)) {
        return false;
    }
    return !((id1 == 0x00 && id2 == 0x00) ||
             (id1 == 0xFF && id2 == 0xFF));
}

bool Es8311Device::write(uint8_t reg, uint8_t value) {
    return control_.writeRegister(reg, value);
}

bool Es8311Device::update(uint8_t reg,
                          uint8_t clear_mask,
                          uint8_t set_mask) {
    uint8_t value = 0;
    if(!control_.readRegister(reg, value)) return false;
    value = static_cast<uint8_t>((value & ~clear_mask) | set_mask);
    return control_.writeRegister(reg, value);
}

}  // namespace firefly
