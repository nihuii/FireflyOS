#pragma once

#include <stddef.h>
#include <stdint.h>

namespace firefly {
namespace protocol {

static constexpr char kServiceUuid[] = "7b7f0001-4f53-4653-8000-ff1e00000001";
static constexpr char kCommandRxUuid[] = "7b7f0002-4f53-4653-8000-ff1e00000001";
static constexpr char kEventTxUuid[] = "7b7f0003-4f53-4653-8000-ff1e00000001";
static constexpr char kBulkControlUuid[] = "7b7f0004-4f53-4653-8000-ff1e00000001";

static constexpr uint8_t kMagic0 = 0x46;
static constexpr uint8_t kMagic1 = 0x46;
static constexpr uint8_t kProtocolVersion = 1;
static constexpr size_t kHeaderSize = 11;
static constexpr size_t kMaxPayload = 1024;
static constexpr size_t kMaxEncodedFrame = kHeaderSize + kMaxPayload;
static constexpr size_t kMaxAttChunk = 180;
static constexpr size_t kFragmentMetadataSize = 2;
static constexpr size_t kMaxFragmentData = kMaxAttChunk - kHeaderSize - kFragmentMetadataSize;
static constexpr size_t kDefaultAttChunk = 20;
static constexpr size_t kMinFragmentData =
    kDefaultAttChunk - kHeaderSize - kFragmentMetadataSize;
static constexpr uint8_t kMaxFragmentCount = static_cast<uint8_t>(
    (kMaxPayload + kMinFragmentData - 1) / kMinFragmentData
);

enum class MessageType : uint8_t {
    Hello = 0x01,
    PairRequest = 0x02,
    PairConfirm = 0x03,
    Ack = 0x04,
    UnpairRequest = 0x05,
    UnpairConfirm = 0x06,
    DeviceState = 0x10,
    SettingsGet = 0x11,
    SettingsSet = 0x12,
    NotificationPush = 0x20,
    NotificationDismiss = 0x21,
    WeatherUpdate = 0x30,
    CalendarUpdate = 0x31,
    MediaCommand = 0x40,
    FindPhone = 0x41,
    FindWatch = 0x42,
    WifiProvision = 0x50,
    BulkTransfer = 0x51,
    OtaControl = 0x60,
    Error = 0x7F
};

enum class WireErrorCode : uint8_t {
    InvalidPayload = 1,
    NoActiveMediaSession = 2,
    MediaAccessRequired = 3,
    SecurityDenied = 4,
    FindPhoneUnavailable = 5,
    PersistenceFailure = 6,
    Unauthorized = 7,
};

enum FrameFlag : uint8_t {
    AckRequired = 0x01,
    IsAck = 0x02,
    Fragment = 0x04,
    LastFragment = 0x08
};

inline bool isKnownMessageType(uint8_t value) {
    switch(static_cast<MessageType>(value)) {
        case MessageType::Hello:
        case MessageType::PairRequest:
        case MessageType::PairConfirm:
        case MessageType::Ack:
        case MessageType::UnpairRequest:
        case MessageType::UnpairConfirm:
        case MessageType::DeviceState:
        case MessageType::SettingsGet:
        case MessageType::SettingsSet:
        case MessageType::NotificationPush:
        case MessageType::NotificationDismiss:
        case MessageType::WeatherUpdate:
        case MessageType::CalendarUpdate:
        case MessageType::MediaCommand:
        case MessageType::FindPhone:
        case MessageType::FindWatch:
        case MessageType::WifiProvision:
        case MessageType::BulkTransfer:
        case MessageType::OtaControl:
        case MessageType::Error:
            return true;
        default:
            return false;
    }
}

inline size_t attChunkLimit(uint16_t negotiated_mtu) {
    if(negotiated_mtu <= 3) return 0;
    const size_t mtu_payload = static_cast<size_t>(negotiated_mtu - 3);
    return mtu_payload < kMaxAttChunk ? mtu_payload : kMaxAttChunk;
}

}  // namespace protocol
}  // namespace firefly
