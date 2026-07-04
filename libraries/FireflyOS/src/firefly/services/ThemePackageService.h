#pragma once

#include <stddef.h>
#include <stdint.h>

namespace firefly {

class StorageService;

struct ThemeManifest {
    uint16_t schema = 1;
    char id[24]{};
    char name[48]{};
    char author[48]{};
    uint32_t palette[5]{};
    char wallpaper[64]{};
    uint16_t wallpaper_width = 0;
    uint16_t wallpaper_height = 0;
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

struct ThemeValidationIssue {
    ThemeValidationError error = ThemeValidationError::None;
    char resource[64]{};
    uint32_t actual = 0;
    uint32_t limit = 0;
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
    bool validatePackage(StorageService & storage,
                         const char * theme_root,
                         ThemeManifest & manifest,
                         ThemeValidationIssue & issue) const;
    bool importPackage(StorageService & storage,
                       const char * theme_root,
                       ThemeValidationIssue * issue = nullptr) const;

    static bool isSafeResourcePath(const char * path);
    static const char * errorText(ThemeValidationError error);
    static const ThemeManifest & neutralDefault();
    static const ThemeManifest & fireflyDefault();
};

}  // namespace firefly
