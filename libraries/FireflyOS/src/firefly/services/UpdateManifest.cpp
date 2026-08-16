#include "UpdateManifest.h"

#include <mbedtls/bignum.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include <string.h>

namespace firefly {
namespace {

constexpr uint32_t kOtaSlotBytes = 0xB00000UL;
constexpr uint8_t kCanonicalMagic[8] = {
    'F', 'F', 'O', 'T', 'A', '1', 0, 0
};

class JsonCursor {
public:
    JsonCursor(const char * input, size_t length)
        : input_(input), length_(length) {}

    void whitespace() {
        while(position_ < length_) {
            const char value = input_[position_];
            if(value != ' ' && value != '\t' &&
               value != '\r' && value != '\n') break;
            ++position_;
        }
    }

    bool take(char expected) {
        whitespace();
        if(position_ >= length_ || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    bool string(char * output, size_t capacity, size_t & output_length) {
        whitespace();
        if(!output || capacity == 0 || position_ >= length_ ||
           input_[position_] != '"') return false;
        ++position_;
        output_length = 0;
        while(position_ < length_) {
            const unsigned char value =
                static_cast<unsigned char>(input_[position_++]);
            if(value == '"') {
                output[output_length] = '\0';
                return true;
            }
            if(value < 0x20 || value == '\\' ||
               output_length + 1 >= capacity) return false;
            output[output_length++] = static_cast<char>(value);
        }
        return false;
    }

    bool uint32(uint32_t & output) {
        whitespace();
        if(position_ >= length_ || input_[position_] < '0' ||
           input_[position_] > '9') return false;
        if(input_[position_] == '0' && position_ + 1 < length_ &&
           input_[position_ + 1] >= '0' && input_[position_ + 1] <= '9') {
            return false;
        }
        uint32_t value = 0;
        do {
            const uint32_t digit =
                static_cast<uint32_t>(input_[position_] - '0');
            if(value > (UINT32_MAX - digit) / 10U) return false;
            value = value * 10U + digit;
            ++position_;
        } while(position_ < length_ && input_[position_] >= '0' &&
                input_[position_] <= '9');
        output = value;
        return true;
    }

    bool finished() {
        whitespace();
        return position_ == length_;
    }

private:
    const char * input_ = nullptr;
    size_t length_ = 0;
    size_t position_ = 0;
};

int hexNibble(char value) {
    if(value >= '0' && value <= '9') return value - '0';
    if(value >= 'a' && value <= 'f') return value - 'a' + 10;
    if(value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool decodeHex(const char * input, size_t input_length,
               uint8_t * output, size_t output_length) {
    if(!input || !output || input_length != output_length * 2U) return false;
    for(size_t index = 0; index < output_length; ++index) {
        const int high = hexNibble(input[index * 2]);
        const int low = hexNibble(input[index * 2 + 1]);
        if(high < 0 || low < 0) return false;
        output[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

void writeLe16(uint8_t * output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
}

void writeLe32(uint8_t * output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

bool terminatedAndNonEmpty(const char * value, size_t capacity) {
    return value && value[0] != '\0' && memchr(value, '\0', capacity) != nullptr;
}

}  // namespace

bool UpdateManifestCodec::parseJson(const char * json,
                                    size_t length,
                                    UpdateManifest & output) {
    if(!json || length == 0 || length > kMaxJsonBytes) return false;

    JsonCursor cursor(json, length);
    UpdateManifest parsed{};
    uint8_t seen = 0;
    if(!cursor.take('{')) return false;

    for(uint8_t field_count = 0; field_count < 8; ++field_count) {
        char key[16]{};
        size_t key_length = 0;
        if(!cursor.string(key, sizeof(key), key_length) ||
           !cursor.take(':')) return false;

        uint8_t bit = 0;
        if(strcmp(key, "schema") == 0) bit = 1U << 0;
        else if(strcmp(key, "product") == 0) bit = 1U << 1;
        else if(strcmp(key, "version") == 0) bit = 1U << 2;
        else if(strcmp(key, "build") == 0) bit = 1U << 3;
        else if(strcmp(key, "min_build") == 0) bit = 1U << 4;
        else if(strcmp(key, "size") == 0) bit = 1U << 5;
        else if(strcmp(key, "sha256") == 0) bit = 1U << 6;
        else if(strcmp(key, "signature") == 0) bit = 1U << 7;
        else return false;
        if((seen & bit) != 0) return false;
        seen |= bit;

        if(bit == (1U << 0)) {
            uint32_t schema = 0;
            if(!cursor.uint32(schema) || schema > UINT16_MAX) return false;
            parsed.schema = static_cast<uint16_t>(schema);
        } else if(bit == (1U << 1)) {
            size_t value_length = 0;
            if(!cursor.string(parsed.product, sizeof(parsed.product),
                              value_length) || value_length == 0) return false;
        } else if(bit == (1U << 2)) {
            size_t value_length = 0;
            if(!cursor.string(parsed.version, sizeof(parsed.version),
                              value_length) || value_length == 0) return false;
        } else if(bit == (1U << 3)) {
            if(!cursor.uint32(parsed.build)) return false;
        } else if(bit == (1U << 4)) {
            if(!cursor.uint32(parsed.min_build)) return false;
        } else if(bit == (1U << 5)) {
            if(!cursor.uint32(parsed.size)) return false;
        } else {
            char encoded[129]{};
            size_t encoded_length = 0;
            if(!cursor.string(encoded, sizeof(encoded), encoded_length)) return false;
            if(bit == (1U << 6)) {
                if(!decodeHex(encoded, encoded_length,
                              parsed.sha256, sizeof(parsed.sha256))) return false;
            } else if(!decodeHex(encoded, encoded_length,
                                 parsed.ecdsa_p256_signature,
                                 sizeof(parsed.ecdsa_p256_signature))) {
                return false;
            }
        }

        if(field_count < 7) {
            if(!cursor.take(',')) return false;
        }
    }

    if(seen != 0xFF || !cursor.take('}') || !cursor.finished() ||
       parsed.schema != 1 || parsed.build <= parsed.min_build ||
       parsed.size == 0 || parsed.size > kOtaSlotBytes) return false;
    output = parsed;
    return true;
}

bool UpdateManifestCodec::canonicalize(
        const UpdateManifest & manifest,
        uint8_t output[kCanonicalBytes]) {
    if(!output || manifest.schema != 1 ||
       !terminatedAndNonEmpty(manifest.product, sizeof(manifest.product)) ||
       !terminatedAndNonEmpty(manifest.version, sizeof(manifest.version)) ||
       manifest.build <= manifest.min_build || manifest.size == 0 ||
       manifest.size > kOtaSlotBytes) return false;

    memset(output, 0, kCanonicalBytes);
    memcpy(output, kCanonicalMagic, sizeof(kCanonicalMagic));
    writeLe16(output + 8, manifest.schema);
    memcpy(output + 10, manifest.product, sizeof(manifest.product));
    memcpy(output + 26, manifest.version, sizeof(manifest.version));
    writeLe32(output + 42, manifest.build);
    writeLe32(output + 46, manifest.min_build);
    writeLe32(output + 50, manifest.size);
    memcpy(output + 54, manifest.sha256, sizeof(manifest.sha256));
    return true;
}

bool UpdateManifestCodec::verifySignature(
        const UpdateManifest & manifest,
        const uint8_t public_key[65]) {
    if(!public_key || public_key[0] != 0x04) return false;

    uint8_t canonical[kCanonicalBytes]{};
    uint8_t digest[32]{};
    if(!canonicalize(manifest, canonical) ||
       mbedtls_sha256_ret(canonical, sizeof(canonical), digest, 0) != 0) {
        return false;
    }

    mbedtls_ecdsa_context context;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecdsa_init(&context);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    bool valid =
        mbedtls_ecp_group_load(&context.grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecp_point_read_binary(&context.grp, &context.Q,
                                      public_key, 65) == 0 &&
        mbedtls_mpi_read_binary(&r, manifest.ecdsa_p256_signature, 32) == 0 &&
        mbedtls_mpi_read_binary(&s,
                                manifest.ecdsa_p256_signature + 32, 32) == 0 &&
        mbedtls_ecdsa_verify(&context.grp, digest, sizeof(digest),
                             &context.Q, &r, &s) == 0;

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecdsa_free(&context);
    return valid;
}

}  // namespace firefly
