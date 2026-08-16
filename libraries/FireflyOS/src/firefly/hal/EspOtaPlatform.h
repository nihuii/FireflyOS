#pragma once

#include <esp_ota_ops.h>

#include "../services/UpdateService.h"
#include "../services/BootValidationService.h"

namespace firefly {

class EspOtaWriter : public UpdateWriter {
public:
    bool begin(uint32_t size) override;
    bool write(const uint8_t * data, size_t size) override;
    bool finish() override;
    bool selectForNextBoot() override;
    void abort() override;

private:
    const esp_partition_t * target_ = nullptr;
    esp_ota_handle_t handle_ = 0;
    bool active_ = false;
    bool finalized_ = false;
};

class EspOtaBootPlatform : public BootValidationPlatform {
public:
    bool pendingVerify() override;
    bool markValid() override;
    bool markInvalidAndRollback() override;
};

}  // namespace firefly
