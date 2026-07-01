#pragma once

#include <stdint.h>

#include "../hal/DeviceInterfaces.h"

namespace firefly {

struct TimeSnapshot {
    bool valid = false;
    int64_t epoch_seconds = 0;
};

class TimeService {
public:
    explicit TimeService(ClockDevice & clock);

    bool begin();
    TimeSnapshot now() const;
    bool setLocalTime(int64_t epoch_seconds);
    TimeSnapshot reloadRtc();
    void tick();

private:
    ClockDevice & clock_;
    TimeSnapshot current_{};
};

}  // namespace firefly
