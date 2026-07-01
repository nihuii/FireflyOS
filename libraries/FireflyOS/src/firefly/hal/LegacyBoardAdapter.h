#pragma once

#include "DeviceInterfaces.h"
#include "I2cBusManager.h"

#include <Arduino_GFX_Library.h>
#include <SensorPCF85063.hpp>

#ifndef XPOWERS_CHIP_AXP2101
#define XPOWERS_CHIP_AXP2101
#endif
#include <XPowersLib.h>

namespace firefly {

class LegacyBoardAdapter final : public ClockDevice, public PowerDevice {
public:
    LegacyBoardAdapter(SensorPCF85063 & rtc,
                       XPowersPMU & power,
                       Arduino_CO5300 & display,
                       I2cBusManager * i2c = nullptr)
        : rtc_(rtc), power_(power), display_(display), i2c_(i2c) {}

    bool readEpoch(int64_t & epoch_seconds) override;
    bool writeEpoch(int64_t epoch_seconds) override;
    BatteryState readBattery() override;
    void setDisplayBrightness(uint8_t value) override;

private:
    bool lockBus();
    void unlockBus();

    SensorPCF85063 & rtc_;
    XPowersPMU & power_;
    Arduino_CO5300 & display_;
    I2cBusManager * i2c_;
};

}  // namespace firefly
