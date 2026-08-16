#pragma once

#include <stddef.h>
#include <stdint.h>
#include <FS.h>
#include <atomic>

namespace firefly {

class SdFailureMonitor {
public:
    bool noteResult(bool success);
    void reset() { consecutive_failures_ = 0; }
    uint8_t consecutiveFailures() const { return consecutive_failures_; }

private:
    uint8_t consecutive_failures_ = 0;
};

class SdCardDevice {
public:
    static constexpr int kClockPin = 2;
    static constexpr int kCommandPin = 1;
    static constexpr int kDataPin = 3;
    static constexpr size_t kMaxRelativePath = 160;

    bool begin();
    void end();
    bool mounted() const {
        return mounted_.load(std::memory_order_acquire);
    }
    uint64_t totalBytes() const;
    uint64_t usedBytes() const;
    bool ensureFireflyDirectories();
    bool exists(const char * relative_path) const;
    bool validateSession() const;
    bool takeRemovedEvent();
    void noteIoResult(bool success) const;
    static fs::FS & filesystem();

    static bool isSafeRelativePath(const char * relative_path);

private:
    static bool makeManagedPath(const char * relative_path,
                                char * out,
                                size_t out_size);
    mutable std::atomic<bool> mounted_{false};
    mutable std::atomic<bool> removed_event_pending_{false};
    mutable SdFailureMonitor failure_monitor_{};
};

}  // namespace firefly
