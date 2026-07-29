#include "FrameCodec.h"

#include <string.h>

namespace firefly {
namespace protocol {

namespace {

uint16_t readLittleEndian16(const uint8_t * input) {
    return static_cast<uint16_t>(input[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(input[1]) << 8);
}

void writeLittleEndian16(uint8_t * output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value & 0xFF);
    output[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

}  // namespace

size_t FrameCodec::encode(const Frame & frame,
                          uint8_t * output,
                          size_t capacity) {
    if(output == nullptr || frame.payload_length > kMaxPayload) return 0;
    const size_t encoded_length = kHeaderSize + frame.payload_length;
    if(capacity < encoded_length) return 0;

    output[0] = kMagic0;
    output[1] = kMagic1;
    output[2] = kProtocolVersion;
    output[3] = static_cast<uint8_t>(frame.type);
    output[4] = frame.flags;
    writeLittleEndian16(output + 5, frame.sequence);
    writeLittleEndian16(output + 7, frame.payload_length);
    if(frame.payload_length > 0) {
        memcpy(output + kHeaderSize, frame.payload, frame.payload_length);
    }

    uint16_t crc = crc16(output, 9);
    crc = crc16(frame.payload, frame.payload_length, crc);
    writeLittleEndian16(output + 9, crc);
    return encoded_length;
}

DecodeError FrameCodec::decode(const uint8_t * input,
                               size_t length,
                               Frame & frame) {
    if(input == nullptr || length < kHeaderSize) return DecodeError::TooShort;
    if(input[0] != kMagic0 || input[1] != kMagic1) return DecodeError::BadMagic;
    if(input[2] != kProtocolVersion) return DecodeError::BadVersion;

    const uint16_t payload_length = readLittleEndian16(input + 7);
    if(payload_length > kMaxPayload) return DecodeError::TooLarge;
    const size_t expected_length = kHeaderSize + payload_length;
    if(length < expected_length) return DecodeError::TooShort;
    if(length > expected_length) return DecodeError::TooLarge;

    uint16_t actual_crc = crc16(input, 9);
    actual_crc = crc16(input + kHeaderSize, payload_length, actual_crc);
    if(actual_crc != readLittleEndian16(input + 9)) {
        return DecodeError::CrcMismatch;
    }

    frame.type = isKnownMessageType(input[3])
        ? static_cast<MessageType>(input[3])
        : MessageType::Error;
    frame.flags = input[4];
    frame.sequence = readLittleEndian16(input + 5);
    frame.payload_length = payload_length;
    if(payload_length > 0) {
        memcpy(frame.payload, input + kHeaderSize, payload_length);
    }
    return DecodeError::None;
}

uint16_t FrameCodec::crc16(const uint8_t * data,
                           size_t length,
                           uint16_t seed) {
    if(data == nullptr && length > 0) return seed;
    uint16_t crc = seed;
    for(size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for(uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000)
                ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

}  // namespace protocol
}  // namespace firefly
