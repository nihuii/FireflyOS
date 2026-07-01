#include "I2cBusManager.h"

namespace firefly {

I2cBusManager::I2cBusManager(TwoWire & wire)
    : wire_(wire), mutex_(xSemaphoreCreateMutexStatic(&mutex_storage_)) {}

bool I2cBusManager::lock(uint32_t timeout_ms) {
    if(!mutex_) {
        return false;
    }
    const TickType_t ticks = timeout_ms == portMAX_DELAY
        ? portMAX_DELAY
        : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(mutex_, ticks) == pdTRUE;
}

void I2cBusManager::unlock() {
    if(mutex_) {
        xSemaphoreGive(mutex_);
    }
}

}  // namespace firefly
