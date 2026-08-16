#include "EspOtaPlatform.h"

namespace firefly {

bool EspOtaWriter::begin(uint32_t size) {
    abort();
    const esp_partition_t * running = esp_ota_get_running_partition();
    target_ = esp_ota_get_next_update_partition(nullptr);
    if(!running || !target_ || target_ == running || size == 0 ||
       size > target_->size) {
        target_ = nullptr;
        return false;
    }
    if(esp_ota_begin(target_, size, &handle_) != ESP_OK) {
        target_ = nullptr;
        handle_ = 0;
        return false;
    }
    active_ = true;
    finalized_ = false;
    return true;
}

bool EspOtaWriter::write(const uint8_t * data, size_t size) {
    return active_ && data && size > 0 && size <= UpdateService::kChunkBytes &&
        esp_ota_write(handle_, data, size) == ESP_OK;
}

bool EspOtaWriter::finish() {
    if(!active_) return false;
    const esp_err_t result = esp_ota_end(handle_);
    active_ = false;
    handle_ = 0;
    finalized_ = result == ESP_OK;
    return finalized_;
}

bool EspOtaWriter::selectForNextBoot() {
    return finalized_ && target_ &&
        esp_ota_set_boot_partition(target_) == ESP_OK;
}

void EspOtaWriter::abort() {
    if(active_) esp_ota_abort(handle_);
    handle_ = 0;
    active_ = false;
    finalized_ = false;
    target_ = nullptr;
}

bool EspOtaBootPlatform::pendingVerify() {
    const esp_partition_t * running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    return running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY;
}

bool EspOtaBootPlatform::markValid() {
    return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}

bool EspOtaBootPlatform::markInvalidAndRollback() {
    return esp_ota_mark_app_invalid_rollback_and_reboot() == ESP_OK;
}

}  // namespace firefly
