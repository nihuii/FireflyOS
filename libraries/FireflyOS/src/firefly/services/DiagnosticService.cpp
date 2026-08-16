#include "DiagnosticService.h"

#include <Arduino.h>
#include <stdio.h>

namespace firefly {
namespace {

constexpr char kDiagnosticPartPath[] =
    "/FireflyOS/Logs/diagnostics.csv.part";
constexpr char kDiagnosticPath[] = "/FireflyOS/Logs/diagnostics.csv";
constexpr char kHeader[] =
    "timestamp_ms,reason,internal_free,internal_minimum,internal_largest,"
    "psram_free,ui_stack_words,background_stack_words,event_drops,"
    "event_size,event_peak,power_mode,restart_reason\n";

DiagnosticRecord makeRecord(uint32_t timestamp_ms,
                            DiagnosticReason reason,
                            const DiagnosticSample & sample) {
    DiagnosticRecord record{};
    record.timestamp_ms = timestamp_ms;
    record.reason = reason;
    record.internal_free = sample.internal_free;
    record.internal_minimum = sample.internal_minimum;
    record.internal_largest = sample.internal_largest;
    record.psram_free = sample.psram_free;
    record.ui_stack_words = sample.ui_stack_words;
    record.background_stack_words = sample.background_stack_words;
    record.event_drops = sample.event_drops;
    record.event_size = sample.event_size;
    record.event_peak = sample.event_peak;
    record.power_mode = sample.power_mode;
    record.restart_reason = sample.restart_reason;
    return record;
}

int formatRecord(char * line, size_t capacity,
                 const DiagnosticRecord & record) {
    return snprintf(
        line, capacity,
        "%lu,%u,%lu,%lu,%lu,%lu,%u,%u,%u,%u,%u,%u,%u\n",
        static_cast<unsigned long>(record.timestamp_ms),
        static_cast<unsigned>(record.reason),
        static_cast<unsigned long>(record.internal_free),
        static_cast<unsigned long>(record.internal_minimum),
        static_cast<unsigned long>(record.internal_largest),
        static_cast<unsigned long>(record.psram_free),
        static_cast<unsigned>(record.ui_stack_words),
        static_cast<unsigned>(record.background_stack_words),
        static_cast<unsigned>(record.event_drops),
        static_cast<unsigned>(record.event_size),
        static_cast<unsigned>(record.event_peak),
        static_cast<unsigned>(record.power_mode),
        static_cast<unsigned>(record.restart_reason));
}

}  // namespace

DiagnosticService::DiagnosticService() {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
}

void DiagnosticService::record(uint32_t timestamp_ms,
                               DiagnosticReason reason,
                               const DiagnosticSample & sample) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const DiagnosticRecord record = makeRecord(timestamp_ms, reason, sample);
    if(count_ < kCapacity) {
        const uint8_t index = static_cast<uint8_t>(
            (static_cast<uint16_t>(head_) + count_) % kCapacity);
        records_[index] = record;
        ++count_;
    } else {
        records_[head_] = record;
        head_ = static_cast<uint8_t>((head_ + 1U) % kCapacity);
    }
    xSemaphoreGive(mutex_);
}

bool DiagnosticService::sampleMinute(
        uint32_t timestamp_ms,
        const DiagnosticSample & sample) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool due = !periodic_started_ ||
        static_cast<uint32_t>(timestamp_ms - last_periodic_ms_) >= kPeriodicMs;
    if(due) {
        periodic_started_ = true;
        last_periodic_ms_ = timestamp_ms;
    }
    xSemaphoreGive(mutex_);
    if(due) record(timestamp_ms, DiagnosticReason::Periodic, sample);
    return due;
}

uint8_t DiagnosticService::count() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint8_t result = count_;
    xSemaphoreGive(mutex_);
    return result;
}

bool DiagnosticService::at(uint8_t index, DiagnosticRecord & output) const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool valid = index < count_;
    if(valid) {
        const uint8_t physical = static_cast<uint8_t>(
            (static_cast<uint16_t>(head_) + index) % kCapacity);
        output = records_[physical];
    }
    xSemaphoreGive(mutex_);
    return valid;
}

bool DiagnosticService::exportTo(DiagnosticExport & output) const {
    if(!output.begin()) return false;
    DiagnosticRecord snapshot[kCapacity]{};
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint8_t total = count_;
    for(uint8_t index = 0; index < total; ++index) {
        const uint8_t physical = static_cast<uint8_t>(
            (static_cast<uint16_t>(head_) + index) % kCapacity);
        snapshot[index] = records_[physical];
    }
    xSemaphoreGive(mutex_);
    for(uint8_t index = 0; index < total; ++index) {
        if(!output.write(snapshot[index])) {
            output.abort();
            return false;
        }
    }
    if(!output.finish()) {
        output.abort();
        return false;
    }
    return true;
}

bool SerialDiagnosticExport::begin() {
    Serial.print("FIREFLY_DIAGNOSTICS_BEGIN\n");
    Serial.print(kHeader);
    return true;
}

bool SerialDiagnosticExport::write(const DiagnosticRecord & record) {
    char line[192]{};
    const int length = formatRecord(line, sizeof(line), record);
    if(length <= 0 || static_cast<size_t>(length) >= sizeof(line)) return false;
    return Serial.write(reinterpret_cast<const uint8_t *>(line),
                        static_cast<size_t>(length)) ==
        static_cast<size_t>(length);
}

bool SerialDiagnosticExport::finish() {
    Serial.print("FIREFLY_DIAGNOSTICS_END\n");
    return true;
}

void SerialDiagnosticExport::abort() {
    Serial.print("FIREFLY_DIAGNOSTICS_ABORT\n");
}

bool SdDiagnosticExport::begin() {
    if(started_ || !storage_.sdAvailable() ||
       storage_.bulkSdSessionActive()) return false;
    if(storage_.managedExists(kDiagnosticPartPath) &&
       !storage_.removeManaged(kDiagnosticPartPath)) return false;
    file_ = storage_.openManaged(kDiagnosticPartPath, FILE_WRITE);
    if(!file_) return false;
    started_ = true;
    const bool written = storage_.writeManaged(
        file_, reinterpret_cast<const uint8_t *>(kHeader),
        sizeof(kHeader) - 1) == sizeof(kHeader) - 1;
    if(!written) abort();
    return written;
}

bool SdDiagnosticExport::write(const DiagnosticRecord & record) {
    if(!started_ || !file_) return false;
    char line[192]{};
    const int length = formatRecord(line, sizeof(line), record);
    return length > 0 && static_cast<size_t>(length) < sizeof(line) &&
        storage_.writeManaged(file_, reinterpret_cast<const uint8_t *>(line),
                              static_cast<size_t>(length)) ==
            static_cast<size_t>(length);
}

bool SdDiagnosticExport::finish() {
    if(!started_) return false;
    storage_.closeManaged(file_);
    started_ = false;
    return storage_.renameManaged(kDiagnosticPartPath, kDiagnosticPath);
}

void SdDiagnosticExport::abort() {
    if(file_) storage_.closeManaged(file_);
    if(started_ || storage_.managedExists(kDiagnosticPartPath)) {
        storage_.removeManaged(kDiagnosticPartPath);
    }
    started_ = false;
}

}  // namespace firefly
