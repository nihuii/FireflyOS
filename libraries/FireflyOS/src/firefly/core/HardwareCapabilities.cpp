#include "HardwareCapabilities.h"

namespace firefly {

HardwareCapabilities::HardwareCapabilities() {
    for(uint8_t index = 0;
        index < static_cast<uint8_t>(HardwareDevice::Count);
        ++index) {
        snapshot_.status[index] = HardwareAvailability::Unavailable;
        snapshot_.failure[index] = HardwareFailure::NotProbed;
    }
}

bool HardwareCapabilities::valid(HardwareDevice device) {
    return static_cast<uint8_t>(device) <
        static_cast<uint8_t>(HardwareDevice::Count);
}

void HardwareCapabilities::set(HardwareDevice device,
                               HardwareAvailability availability,
                               HardwareFailure failure_code) {
    if(!valid(device)) return;
    if(availability == HardwareAvailability::Available) {
        failure_code = HardwareFailure::None;
    } else if(failure_code == HardwareFailure::None) {
        failure_code = HardwareFailure::InitFailed;
    }
    const uint8_t index = static_cast<uint8_t>(device);
    portENTER_CRITICAL(&mux_);
    snapshot_.status[index] = availability;
    snapshot_.failure[index] = failure_code;
    portEXIT_CRITICAL(&mux_);
}

bool HardwareCapabilities::available(HardwareDevice device) const {
    return status(device) != HardwareAvailability::Unavailable;
}

HardwareAvailability HardwareCapabilities::status(
        HardwareDevice device) const {
    if(!valid(device)) return HardwareAvailability::Unavailable;
    const uint8_t index = static_cast<uint8_t>(device);
    portENTER_CRITICAL(&mux_);
    const HardwareAvailability result = snapshot_.status[index];
    portEXIT_CRITICAL(&mux_);
    return result;
}

HardwareFailure HardwareCapabilities::failure(HardwareDevice device) const {
    if(!valid(device)) return HardwareFailure::NotProbed;
    const uint8_t index = static_cast<uint8_t>(device);
    portENTER_CRITICAL(&mux_);
    const HardwareFailure result = snapshot_.failure[index];
    portEXIT_CRITICAL(&mux_);
    return result;
}

HardwareCapabilitySnapshot HardwareCapabilities::snapshot() const {
    portENTER_CRITICAL(&mux_);
    const HardwareCapabilitySnapshot result = snapshot_;
    portEXIT_CRITICAL(&mux_);
    return result;
}

const char * HardwareCapabilities::deviceName(HardwareDevice device) {
    switch(device) {
        case HardwareDevice::Rtc: return "RTC";
        case HardwareDevice::Pmu: return "PMU";
        case HardwareDevice::Imu: return "IMU";
        case HardwareDevice::Sd: return "SD";
        case HardwareDevice::Codec: return "Codec";
        case HardwareDevice::Ble: return "BLE";
        case HardwareDevice::Wifi: return "Wi-Fi";
        default: return "Unknown";
    }
}

const char * HardwareCapabilities::statusText(
        HardwareAvailability availability) {
    switch(availability) {
        case HardwareAvailability::Available: return "Available";
        case HardwareAvailability::Degraded: return "Degraded";
        default: return "Unavailable";
    }
}

const char * HardwareCapabilities::failureText(HardwareFailure failure_code) {
    switch(failure_code) {
        case HardwareFailure::None: return "None";
        case HardwareFailure::NotProbed: return "Not probed";
        case HardwareFailure::NotDetected: return "Not detected";
        case HardwareFailure::InitFailed: return "Init failed";
        case HardwareFailure::IoFailure: return "I/O failure";
        case HardwareFailure::Timeout: return "Timeout";
        case HardwareFailure::Busy: return "Busy";
        default: return "Unknown";
    }
}

}  // namespace firefly
