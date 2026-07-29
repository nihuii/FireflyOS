#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ProtocolTypes.h"

namespace firefly {
namespace protocol {

struct Frame {
    MessageType type = MessageType::Error;
    uint8_t flags = 0;
    uint16_t sequence = 0;
    uint16_t payload_length = 0;
    uint8_t payload[kMaxPayload]{};
};

enum class DecodeError : uint8_t {
    None,
    TooShort,
    BadMagic,
    BadVersion,
    TooLarge,
    CrcMismatch
};

class FrameCodec {
public:
    static size_t encode(const Frame & frame, uint8_t * output, size_t capacity);
    static DecodeError decode(const uint8_t * input, size_t length, Frame & frame);
    static uint16_t crc16(const uint8_t * data,
                          size_t length,
                          uint16_t seed = 0xFFFF);
};

static_assert(sizeof(Frame::payload) == kMaxPayload,
              "BLE frame payload must remain fixed capacity");

}  // namespace protocol
}  // namespace firefly
