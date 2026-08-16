#pragma once

#include <FS.h>
#include <WiFiClientSecure.h>
#include <stddef.h>
#include <stdint.h>

#include "StorageService.h"
#include "UpdateReleaseConfig.h"
#include "UpdateService.h"

namespace firefly {

class UpdateManifestSource {
public:
    virtual ~UpdateManifestSource() = default;
    virtual UpdateIoResult fetch(UpdateManifest & output) = 0;
};

class SdManifestSource : public UpdateManifestSource {
public:
    SdManifestSource(StorageService & storage, const char * managed_path);
    UpdateIoResult fetch(UpdateManifest & output) override;

private:
    StorageService & storage_;
    char path_[192]{};
};

class SdUpdateSource : public UpdateSource {
public:
    SdUpdateSource(StorageService & storage, const char * managed_path);

    UpdateIoResult open(const UpdateManifest & manifest) override;
    UpdateIoResult read(uint8_t * output,
                        size_t capacity,
                        size_t & output_length) override;
    void close() override;

    static bool validManagedUpdatePath(const char * path);

private:
    StorageService & storage_;
    char path_[192]{};
    fs::File file_{};
    uint32_t expected_size_ = 0;
    bool session_open_ = false;
};

class HttpsUpdateSource : public UpdateSource {
public:
    explicit HttpsUpdateSource(
        const char * filename = FIREFLY_UPDATE_FIRMWARE_FILE);

    UpdateIoResult open(const UpdateManifest & manifest) override;
    UpdateIoResult read(uint8_t * output,
                        size_t capacity,
                        size_t & output_length) override;
    void close() override;

    static bool configured();
    static bool validFilename(const char * filename);

private:
    UpdateIoResult readLine(char * output, size_t capacity,
                            size_t & total_header_bytes);

    WiFiClientSecure client_{};
    char filename_[65]{};
    uint32_t expected_size_ = 0;
    uint32_t received_size_ = 0;
    uint32_t opened_ms_ = 0;
    uint32_t last_read_ms_ = 0;
    bool open_ = false;
};

class HttpsManifestSource : public UpdateManifestSource {
public:
    explicit HttpsManifestSource(
        const char * filename = FIREFLY_UPDATE_MANIFEST_FILE);

    UpdateIoResult fetch(UpdateManifest & output) override;
    static bool configured();
    static bool validFilename(const char * filename);

private:
    UpdateIoResult readLine(char * output, size_t capacity,
                            size_t & total_header_bytes,
                            uint32_t opened_ms);

    WiFiClientSecure client_{};
    char filename_[65]{};
};

}  // namespace firefly
