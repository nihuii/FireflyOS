#include "NotificationService.h"

#include <string.h>

namespace firefly {
namespace {

constexpr uint8_t kNotificationSchema = 1;
constexpr uint16_t kPushHeaderSize = 19;
constexpr uint16_t kDismissHeaderSize = 3;
constexpr uint16_t kMaxKeyBytes = 39;
constexpr uint16_t kMaxPackageBytes = 95;
constexpr uint16_t kMaxAppNameBytes = 31;
constexpr uint16_t kMaxTitleBytes = 128;
constexpr uint16_t kMaxBodyBytes = 256;

uint16_t readU16(const uint8_t * input) {
    return static_cast<uint16_t>(
        input[0] | (static_cast<uint16_t>(input[1]) << 8)
    );
}

int64_t readI64(const uint8_t * input) {
    uint64_t value = 0;
    for(uint8_t index = 0; index < 8; ++index) {
        value |= static_cast<uint64_t>(input[index]) << (index * 8);
    }
    return static_cast<int64_t>(value);
}

bool validUtf8(const uint8_t * input, uint16_t length) {
    uint16_t index = 0;
    while(index < length) {
        const uint8_t first = input[index];
        uint8_t extra = 0;
        uint32_t codepoint = 0;
        if(first <= 0x7F) {
            if(first == 0) return false;
            codepoint = first;
        } else if(first >= 0xC2 && first <= 0xDF) {
            extra = 1;
            codepoint = first & 0x1F;
        } else if(first >= 0xE0 && first <= 0xEF) {
            extra = 2;
            codepoint = first & 0x0F;
        } else if(first >= 0xF0 && first <= 0xF4) {
            extra = 3;
            codepoint = first & 0x07;
        } else {
            return false;
        }
        if(static_cast<uint16_t>(index + extra) >= length) return false;
        for(uint8_t offset = 1; offset <= extra; ++offset) {
            const uint8_t continuation = input[index + offset];
            if((continuation & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }
        if((extra == 2 && codepoint < 0x800) ||
           (extra == 3 && codepoint < 0x10000) ||
           (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
           codepoint > 0x10FFFF) {
            return false;
        }
        index = static_cast<uint16_t>(index + extra + 1);
    }
    return true;
}

bool copyText(char * output,
              size_t capacity,
              const uint8_t * input,
              uint16_t length) {
    if(!output || capacity == 0 || !input || length >= capacity ||
       !validUtf8(input, length)) {
        return false;
    }
    if(length > 0) memcpy(output, input, length);
    output[length] = '\0';
    return true;
}

}  // namespace

bool NotificationService::applyFrame(const protocol::Frame & frame) {
    if(frame.type == protocol::MessageType::NotificationPush) {
        return applyPushPayload(frame.payload, frame.payload_length);
    }
    if(frame.type == protocol::MessageType::NotificationDismiss) {
        return applyDismissPayload(frame.payload, frame.payload_length);
    }
    return false;
}

bool NotificationService::push(const NotificationSummary & summary) {
    if(summary.key[0] == '\0') return false;
    const int8_t existing = findIndex(summary.key);
    if(existing >= 0) {
        const bool locally_pinned = summaries_[existing].pinned;
        summaries_[existing] = summary;
        summaries_[existing].pinned = locally_pinned;
        return true;
    }
    if(count_ < kCapacity) {
        summaries_[count_++] = summary;
        return true;
    }
    const int8_t replace = oldestUnpinnedIndex();
    if(replace < 0) return false;
    summaries_[replace] = summary;
    return true;
}

bool NotificationService::dismiss(const char * key) {
    const int8_t index = findIndex(key);
    if(index < 0) return false;
    for(uint8_t move = static_cast<uint8_t>(index);
        move + 1 < count_;
        ++move) {
        summaries_[move] = summaries_[move + 1];
    }
    --count_;
    summaries_[count_] = NotificationSummary{};
    return true;
}

void NotificationService::clearLocal() {
    for(uint8_t index = 0; index < count_; ++index) {
        summaries_[index] = NotificationSummary{};
    }
    count_ = 0;
}

bool NotificationService::clearSensitiveState() {
    clearLocal();
    phone_connected_ = false;
    lock_screen_body_hidden_ = true;
    return true;
}

void NotificationService::setPhoneConnected(bool connected) {
    phone_connected_ = connected;
}

void NotificationService::setLockScreenBodyHidden(bool hidden) {
    lock_screen_body_hidden_ = hidden;
}

bool NotificationService::copyForDisplay(
    uint8_t index,
    bool lock_screen,
    NotificationSummary & output) const {
    if(index >= count_) return false;
    output = summaries_[index];
    if(lock_screen && lock_screen_body_hidden_) output.body[0] = '\0';
    return true;
}

bool NotificationService::contains(const char * key) const {
    return findIndex(key) >= 0;
}

bool NotificationService::applyPushPayload(const uint8_t * payload,
                                           uint16_t length) {
    if(!payload || length < kPushHeaderSize || payload[0] != kNotificationSchema) {
        return false;
    }
    const uint16_t key_length = readU16(payload + 1);
    const uint16_t package_length = readU16(payload + 3);
    const uint16_t app_length = readU16(payload + 5);
    const uint16_t title_length = readU16(payload + 7);
    const uint16_t body_length = readU16(payload + 9);
    const uint32_t text_length = static_cast<uint32_t>(key_length) +
        package_length + app_length + title_length + body_length;
    if(key_length == 0 || key_length > kMaxKeyBytes ||
       package_length > kMaxPackageBytes ||
       app_length > kMaxAppNameBytes ||
       title_length > kMaxTitleBytes || body_length > kMaxBodyBytes ||
       text_length + kPushHeaderSize != length) {
        return false;
    }

    NotificationSummary summary{};
    summary.posted_epoch = readI64(payload + 11);
    uint16_t offset = kPushHeaderSize;
    if(!copyText(summary.key, sizeof(summary.key), payload + offset,
                 key_length)) {
        return false;
    }
    offset = static_cast<uint16_t>(offset + key_length);
    // packageName is authenticated and UTF-8 validated on the wire, but is not
    // retained because NotificationSummary must remain within its 480-byte cap.
    if(!validUtf8(payload + offset, package_length)) return false;
    offset = static_cast<uint16_t>(offset + package_length);
    if(!copyText(summary.app_name, sizeof(summary.app_name), payload + offset,
                 app_length)) {
        return false;
    }
    offset = static_cast<uint16_t>(offset + app_length);
    if(!copyText(summary.title, sizeof(summary.title), payload + offset,
                 title_length)) {
        return false;
    }
    offset = static_cast<uint16_t>(offset + title_length);
    if(!copyText(summary.body, sizeof(summary.body), payload + offset,
                 body_length)) {
        return false;
    }
    return push(summary);
}

bool NotificationService::applyDismissPayload(const uint8_t * payload,
                                              uint16_t length) {
    if(!payload || length < kDismissHeaderSize ||
       payload[0] != kNotificationSchema) {
        return false;
    }
    const uint16_t key_length = readU16(payload + 1);
    if(key_length > kMaxKeyBytes ||
       static_cast<uint16_t>(kDismissHeaderSize + key_length) != length) {
        return false;
    }
    if(key_length == 0) {
        clearLocal();
        return true;
    }
    char key[40]{};
    if(!copyText(key, sizeof(key), payload + kDismissHeaderSize, key_length)) {
        return false;
    }
    dismiss(key);
    return true;
}

int8_t NotificationService::findIndex(const char * key) const {
    if(!key || key[0] == '\0') return -1;
    for(uint8_t index = 0; index < count_; ++index) {
        if(strncmp(summaries_[index].key, key,
                   sizeof(summaries_[index].key)) == 0) {
            return static_cast<int8_t>(index);
        }
    }
    return -1;
}

int8_t NotificationService::oldestUnpinnedIndex() const {
    int8_t oldest = -1;
    for(uint8_t index = 0; index < count_; ++index) {
        if(summaries_[index].pinned) continue;
        if(oldest < 0 ||
           summaries_[index].posted_epoch <
               summaries_[static_cast<uint8_t>(oldest)].posted_epoch) {
            oldest = static_cast<int8_t>(index);
        }
    }
    return oldest;
}

}  // namespace firefly
