#pragma once

#include <stdint.h>

#include "../core/NotificationModel.h"
#include "../protocol/FrameCodec.h"

namespace firefly {

class NotificationService {
public:
    static constexpr uint8_t kCapacity = 20;

    bool applyFrame(const protocol::Frame & frame);
    bool push(const NotificationSummary & summary);
    bool dismiss(const char * key);
    void clearLocal();
    void setPhoneConnected(bool connected);
    bool phoneConnected() const { return phone_connected_; }
    void setLockScreenBodyHidden(bool hidden);
    bool copyForDisplay(uint8_t index,
                        bool lock_screen,
                        NotificationSummary & output) const;
    bool contains(const char * key) const;
    uint8_t count() const { return count_; }
    const NotificationSummary * summaries() const { return summaries_; }

private:
    bool applyPushPayload(const uint8_t * payload, uint16_t length);
    bool applyDismissPayload(const uint8_t * payload, uint16_t length);
    int8_t findIndex(const char * key) const;
    int8_t oldestUnpinnedIndex() const;

    NotificationSummary summaries_[kCapacity]{};
    uint8_t count_ = 0;
    bool phone_connected_ = false;
    bool lock_screen_body_hidden_ = true;
};

static_assert(sizeof(NotificationSummary) <= 480,
              "notification summaries must remain bounded");

}  // namespace firefly
