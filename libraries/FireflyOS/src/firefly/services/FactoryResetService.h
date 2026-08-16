#pragma once

#include <freertos/FreeRTOS.h>
#include <stdint.h>

namespace firefly {

enum class FactoryResetState : uint8_t {
    Idle,
    Preview,
    InternalConfirmed,
    SdConfirmed,
    Running,
    Completed,
    Failed
};

enum class FactoryResetFailure : uint8_t {
    None,
    InvalidConfirmation,
    InternalClearFailed,
    SdClearFailed,
    RebootFailed
};

struct FactoryResetSnapshot {
    FactoryResetState state = FactoryResetState::Idle;
    FactoryResetFailure failure = FactoryResetFailure::None;
    uint32_t generation = 0;
    bool erase_sd = false;
};

class FactoryResetOwners {
public:
    virtual ~FactoryResetOwners() = default;
    virtual bool clearPairing() = 0;
    virtual bool clearWifi() = 0;
    virtual bool clearNotifications() = 0;
    virtual bool clearWeather() = 0;
    virtual bool clearSettings() = 0;
    virtual bool clearCaches() = 0;
    virtual bool clearManagedSdRoot() = 0;
};

class FactoryResetRebooter {
public:
    virtual ~FactoryResetRebooter() = default;
    virtual bool requestReboot() = 0;
};

class FactoryResetService {
public:
    FactoryResetService(FactoryResetOwners & owners,
                        FactoryResetRebooter & rebooter)
        : owners_(owners), rebooter_(rebooter) {}

    uint32_t beginRequest();
    bool confirmInternal(uint32_t generation);
    bool confirmSdErase(uint32_t generation);
    bool execute(bool erase_sd);
    bool cancel();
    FactoryResetSnapshot snapshot() const;

private:
    FactoryResetOwners & owners_;
    FactoryResetRebooter & rebooter_;
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    FactoryResetSnapshot snapshot_{};
    uint32_t next_generation_ = 0;
};

static_assert(sizeof(FactoryResetSnapshot) <= 12,
              "factory reset snapshot must remain fixed and bounded");

}  // namespace firefly
