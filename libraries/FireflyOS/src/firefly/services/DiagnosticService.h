#pragma once

#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stddef.h>
#include <stdint.h>

#include "PowerService.h"
#include "StorageService.h"

namespace firefly {

enum class DiagnosticReason : uint8_t {
    Periodic,
    SessionBoundary,
    WifiStart,
    WifiEnd,
    BulkStart,
    BulkEnd,
    OtaStart,
    OtaEnd,
    AudioStart,
    AudioEnd,
    RecorderStart,
    RecorderEnd,
    SdMounted,
    SdRemoved,
    BootValidationStart,
    BootValidationEnd,
    Manual
};

struct DiagnosticSample {
    uint32_t internal_free = 0;
    uint32_t internal_minimum = 0;
    uint32_t internal_largest = 0;
    uint32_t psram_free = 0;
    uint16_t ui_stack_words = 0;
    uint16_t background_stack_words = 0;
    uint16_t event_drops = 0;
    uint8_t event_size = 0;
    uint8_t event_peak = 0;
    PowerMode power_mode = PowerMode::Active;
    uint8_t restart_reason = 0;
};

struct DiagnosticRecord {
    uint32_t timestamp_ms = 0;
    DiagnosticReason reason = DiagnosticReason::Periodic;
    uint32_t internal_free = 0;
    uint32_t internal_minimum = 0;
    uint32_t internal_largest = 0;
    uint32_t psram_free = 0;
    uint16_t ui_stack_words = 0;
    uint16_t background_stack_words = 0;
    uint16_t event_drops = 0;
    uint8_t event_size = 0;
    uint8_t event_peak = 0;
    PowerMode power_mode = PowerMode::Active;
    uint8_t restart_reason = 0;
};

class DiagnosticExport {
public:
    virtual ~DiagnosticExport() = default;
    virtual bool begin() = 0;
    virtual bool write(const DiagnosticRecord & record) = 0;
    virtual bool finish() = 0;
    virtual void abort() = 0;
};

class DiagnosticService {
public:
    static constexpr uint8_t kCapacity = 64;
    static constexpr uint32_t kPeriodicMs = 60000;

    DiagnosticService();

    void record(uint32_t timestamp_ms,
                DiagnosticReason reason,
                const DiagnosticSample & sample);
    bool sampleMinute(uint32_t timestamp_ms,
                      const DiagnosticSample & sample);
    uint8_t count() const;
    bool at(uint8_t index, DiagnosticRecord & output) const;
    bool exportTo(DiagnosticExport & output) const;

private:
    DiagnosticRecord records_[kCapacity]{};
    mutable StaticSemaphore_t mutex_storage_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    uint32_t last_periodic_ms_ = 0;
    uint8_t head_ = 0;
    uint8_t count_ = 0;
    bool periodic_started_ = false;
};

class SerialDiagnosticExport : public DiagnosticExport {
public:
    bool begin() override;
    bool write(const DiagnosticRecord & record) override;
    bool finish() override;
    void abort() override;
};

class SdDiagnosticExport : public DiagnosticExport {
public:
    explicit SdDiagnosticExport(StorageService & storage)
        : storage_(storage) {}
    bool begin() override;
    bool write(const DiagnosticRecord & record) override;
    bool finish() override;
    void abort() override;

private:
    StorageService & storage_;
    fs::File file_{};
    bool started_ = false;
};

static_assert(sizeof(DiagnosticRecord) <= 40,
              "diagnostic records must remain fixed and bounded");

}  // namespace firefly
