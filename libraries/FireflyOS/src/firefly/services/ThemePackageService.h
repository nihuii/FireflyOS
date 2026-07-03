#pragma once

#include <stddef.h>
#include <stdint.h>

namespace fs { class FS; }

namespace firefly {

class StorageService;

struct ThemeManifest {
    uint16_t schema = 1;
    char id[24]{};
    char name[48]{};
    char author[48]{};
    uint32_t palette[5]{};
    char wallpaper[64]{};
    char glance[64]{};
    char icon_pack[64]{};
};

enum class ThemeValidationError : uint8_t {
    None,
    ManifestMissing,
    ManifestTooLarge,
    MalformedJson,
    UnsupportedSchema,
    InvalidId,
    InvalidPalette,
    UnsafeResourcePath,
    UnsupportedResource,
    ResourceMissing,
    ResourceTooLarge,
    ResourceInvalid,
    StorageUnavailable,
};

class ThemePackageService {
public:
    static constexpr size_t kMaxManifestBytes = 2048;
    static constexpr size_t kMaxWallpaperBytes = 410 * 502 * 2;
    static constexpr size_t kMaxGlanceBytes = 512 * 1024;
    static constexpr size_t kMaxIconPackBytes = 512 * 1024;
    static constexpr uint16_t kMaxIconFiles = 64;

    bool parseManifest(const char * json,
                       size_t length,
                       ThemeManifest & manifest,
                       ThemeValidationError & error) const;
    bool importPackage(fs::FS & filesystem,
                       const char * theme_root,
                       StorageService & storage,
                       ThemeValidationError * error = nullptr) const;

    static bool isSafeResourcePath(const char * path);
    static const ThemeManifest & neutralDefault();
    static const ThemeManifest & fireflyDefault();
};

}  // namespace firefly
