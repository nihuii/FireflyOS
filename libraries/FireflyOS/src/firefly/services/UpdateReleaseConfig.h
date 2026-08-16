#pragma once

// Production update endpoints and trust material are managed outside the
// repository. Development builds remain SD-update capable when this local
// configuration is absent; release builds fail closed.
#if __has_include("FireflyUpdateConfig.local.h")
#include "FireflyUpdateConfig.local.h"
#define FIREFLY_UPDATE_CONFIGURED 1
#elif defined(FIREFLY_RELEASE_BUILD)
#error "Release OTA builds require FireflyUpdateConfig.local.h"
#else
#define FIREFLY_UPDATE_CONFIGURED 0
#define FIREFLY_UPDATE_BASE_URL ""
#define FIREFLY_UPDATE_HOST ""
#define FIREFLY_UPDATE_CA_CERT ""
#define FIREFLY_UPDATE_MANIFEST_FILE "update.json"
#define FIREFLY_UPDATE_FIRMWARE_FILE "update.bin"
#endif

#if FIREFLY_UPDATE_CONFIGURED
#if !defined(FIREFLY_UPDATE_BASE_URL) || !defined(FIREFLY_UPDATE_HOST) || \
    !defined(FIREFLY_UPDATE_CA_CERT) || \
    !defined(FIREFLY_UPDATE_MANIFEST_FILE) || \
    !defined(FIREFLY_UPDATE_FIRMWARE_FILE)
#error "FireflyUpdateConfig.local.h is missing required OTA definitions"
#endif
static_assert(sizeof(FIREFLY_UPDATE_BASE_URL) > sizeof("https://"),
              "release OTA base URL must be a non-empty HTTPS URL");
static_assert(sizeof(FIREFLY_UPDATE_HOST) > 1,
              "release OTA host must not be empty");
static_assert(sizeof(FIREFLY_UPDATE_CA_CERT) > 1,
              "release OTA CA certificate must not be empty");
static_assert(sizeof(FIREFLY_UPDATE_MANIFEST_FILE) > sizeof(".json"),
              "release OTA manifest filename must not be empty");
static_assert(sizeof(FIREFLY_UPDATE_FIRMWARE_FILE) > sizeof(".bin"),
              "release OTA firmware filename must not be empty");
#endif
