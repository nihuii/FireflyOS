#pragma once

#include <stddef.h>
#include <stdint.h>

namespace firefly {

struct UpdateManifest {
    uint16_t schema = 0;
    char product[16]{};
    char version[16]{};
    uint32_t build = 0;
    uint32_t min_build = 0;
    uint32_t size = 0;
    uint8_t sha256[32]{};
    uint8_t ecdsa_p256_signature[64]{};
};

class UpdateManifestCodec {
public:
    static constexpr size_t kMaxJsonBytes = 1024;
    static constexpr size_t kCanonicalBytes = 86;

    static bool parseJson(const char * json,
                          size_t length,
                          UpdateManifest & output);
    static bool canonicalize(const UpdateManifest & manifest,
                             uint8_t output[kCanonicalBytes]);
    static bool verifySignature(const UpdateManifest & manifest,
                                const uint8_t public_key[65]);
};

static_assert(sizeof(UpdateManifest) <= 144,
              "update manifest must remain fixed and bounded");

}  // namespace firefly
