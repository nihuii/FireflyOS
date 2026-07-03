#include "ThemePackageService.h"

#include <FS.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "StorageService.h"

#ifndef FIREFLYOS_INCLUDE_FIREFLY_THEME
#define FIREFLYOS_INCLUDE_FIREFLY_THEME 1
#endif

namespace firefly {
namespace {

const char * skipSpace(const char * cursor) {
    while(cursor && *cursor && isspace(static_cast<unsigned char>(*cursor))) {
        ++cursor;
    }
    return cursor;
}

const char * findValue(const char * json, const char * key) {
    char pattern[40];
    const int length = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if(length <= 0 || static_cast<size_t>(length) >= sizeof(pattern)) return nullptr;
    const char * cursor = strstr(json, pattern);
    if(!cursor) return nullptr;
    cursor = strchr(cursor + length, ':');
    return cursor ? skipSpace(cursor + 1) : nullptr;
}

bool readString(const char * json,
                const char * key,
                char * out,
                size_t out_size) {
    const char * cursor = findValue(json, key);
    if(!cursor || *cursor != '"' || !out || out_size < 2) return false;
    ++cursor;
    size_t written = 0;
    while(*cursor && *cursor != '"') {
        const unsigned char value = static_cast<unsigned char>(*cursor++);
        if(value < 0x20 || value == '\\' || written + 1 >= out_size) return false;
        out[written++] = static_cast<char>(value);
    }
    if(*cursor != '"' || written == 0) return false;
    out[written] = '\0';
    return true;
}

bool readUnsigned(const char * json, const char * key, uint32_t & value) {
    const char * cursor = findValue(json, key);
    if(!cursor || !isdigit(static_cast<unsigned char>(*cursor))) return false;
    char * end = nullptr;
    const unsigned long parsed = strtoul(cursor, &end, 10);
    if(end == cursor) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

int hexDigit(char value) {
    if(value >= '0' && value <= '9') return value - '0';
    if(value >= 'a' && value <= 'f') return value - 'a' + 10;
    if(value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool readColor(const char * json, const char * key, uint32_t & color) {
    char text[8]{};
    if(!readString(json, key, text, sizeof(text)) || strlen(text) != 7 ||
       text[0] != '#') {
        return false;
    }
    color = 0;
    for(uint8_t i = 1; i < 7; ++i) {
        const int digit = hexDigit(text[i]);
        if(digit < 0) return false;
        color = (color << 4) | static_cast<uint32_t>(digit);
    }
    return true;
}

bool validThemeId(const char * id) {
    if(!id || !id[0]) return false;
    const size_t length = strlen(id);
    if(length >= sizeof(ThemeManifest::id)) return false;
    for(size_t i = 0; i < length; ++i) {
        const char value = id[i];
        if(!((value >= 'a' && value <= 'z') ||
             (value >= '0' && value <= '9') || value == '-')) {
            return false;
        }
    }
    return true;
}

bool endsWith(const char * value, const char * suffix) {
    if(!value || !suffix) return false;
    const size_t value_length = strlen(value);
    const size_t suffix_length = strlen(suffix);
    return value_length >= suffix_length &&
           strcmp(value + value_length - suffix_length, suffix) == 0;
}

bool joinPath(const char * root,
              const char * relative,
              char * out,
              size_t out_size) {
    if(!root || !relative || !out) return false;
    const int written = snprintf(out, out_size, "%s/%s", root, relative);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

bool validThemeRoot(const char * root, const char * id) {
    static const char prefix[] = "/FireflyOS/Themes/";
    if(!root || strncmp(root, prefix, sizeof(prefix) - 1) != 0) return false;
    const char * tail = root + sizeof(prefix) - 1;
    return validThemeId(tail) && strcmp(tail, id) == 0;
}

uint32_t readBigEndian32(const uint8_t * bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
           static_cast<uint32_t>(bytes[3]);
}

bool validateWallpaper(fs::FS & filesystem, const char * path) {
    fs::File file = filesystem.open(path, FILE_READ);
    if(!file || file.isDirectory()) return false;
    const size_t size = file.size();
    uint8_t sample[2]{};
    const bool valid = size >= sizeof(sample) &&
        size <= ThemePackageService::kMaxWallpaperBytes &&
        (size % sizeof(uint16_t)) == 0 &&
        file.read(sample, sizeof(sample)) == sizeof(sample);
    file.close();
    return valid;
}

bool validatePng(fs::FS & filesystem, const char * path) {
    fs::File file = filesystem.open(path, FILE_READ);
    if(!file || file.isDirectory() || file.size() > ThemePackageService::kMaxGlanceBytes) {
        if(file) file.close();
        return false;
    }
    uint8_t header[24]{};
    const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    const bool read = file.read(header, sizeof(header)) == sizeof(header);
    file.close();
    if(!read || memcmp(header, signature, sizeof(signature)) != 0 ||
       memcmp(header + 12, "IHDR", 4) != 0) {
        return false;
    }
    const uint32_t width = readBigEndian32(header + 16);
    const uint32_t height = readBigEndian32(header + 20);
    return width > 0 && height > 0 && width <= 410 && height <= 502;
}

bool validateIconPack(fs::FS & filesystem, const char * path) {
    fs::File directory = filesystem.open(path, FILE_READ);
    if(!directory || !directory.isDirectory()) return false;
    size_t total = 0;
    uint16_t count = 0;
    fs::File entry = directory.openNextFile();
    while(entry) {
        const char * name = entry.name();
        if(entry.isDirectory() ||
           (!endsWith(name, ".png") && !endsWith(name, ".rgb565"))) {
            entry.close();
            directory.close();
            return false;
        }
        total += entry.size();
        ++count;
        entry.close();
        if(count > ThemePackageService::kMaxIconFiles ||
           total > ThemePackageService::kMaxIconPackBytes) {
            directory.close();
            return false;
        }
        entry = directory.openNextFile();
    }
    directory.close();
    return count > 0;
}

ThemeManifest makeBuiltIn(const char * id,
                          const char * name,
                          const uint32_t palette[5]) {
    ThemeManifest manifest{};
    strlcpy(manifest.id, id, sizeof(manifest.id));
    strlcpy(manifest.name, name, sizeof(manifest.name));
    strlcpy(manifest.author, "FireflyOS", sizeof(manifest.author));
    memcpy(manifest.palette, palette, sizeof(manifest.palette));
    return manifest;
}

}  // namespace

bool ThemePackageService::parseManifest(const char * json,
                                        size_t length,
                                        ThemeManifest & manifest,
                                        ThemeValidationError & error) const {
    manifest = ThemeManifest{};
    error = ThemeValidationError::None;
    if(!json || length == 0) {
        error = ThemeValidationError::ManifestMissing;
        return false;
    }
    if(length > kMaxManifestBytes) {
        error = ThemeValidationError::ManifestTooLarge;
        return false;
    }
    char bounded[kMaxManifestBytes + 1];
    memcpy(bounded, json, length);
    bounded[length] = '\0';
    const char * first = skipSpace(bounded);
    if(*first != '{') {
        error = ThemeValidationError::MalformedJson;
        return false;
    }

    uint32_t schema = 0;
    if(!readUnsigned(bounded, "schema", schema)) {
        error = ThemeValidationError::MalformedJson;
        return false;
    }
    if(schema != 1) {
        error = ThemeValidationError::UnsupportedSchema;
        return false;
    }
    manifest.schema = static_cast<uint16_t>(schema);
    if(!readString(bounded, "id", manifest.id, sizeof(manifest.id)) ||
       !validThemeId(manifest.id)) {
        error = ThemeValidationError::InvalidId;
        return false;
    }
    if(!readString(bounded, "name", manifest.name, sizeof(manifest.name)) ||
       !readString(bounded, "author", manifest.author, sizeof(manifest.author))) {
        error = ThemeValidationError::MalformedJson;
        return false;
    }
    const char * color_keys[] = {
        "bg_base", "bg_surface", "primary", "secondary", "critical"
    };
    for(uint8_t i = 0; i < 5; ++i) {
        if(!readColor(bounded, color_keys[i], manifest.palette[i])) {
            error = ThemeValidationError::InvalidPalette;
            return false;
        }
    }
    if(!readString(bounded, "wallpaper", manifest.wallpaper,
                   sizeof(manifest.wallpaper)) ||
       !readString(bounded, "glance", manifest.glance,
                   sizeof(manifest.glance)) ||
       !readString(bounded, "icon_pack", manifest.icon_pack,
                   sizeof(manifest.icon_pack))) {
        error = ThemeValidationError::MalformedJson;
        return false;
    }
    if(!isSafeResourcePath(manifest.wallpaper) ||
       !isSafeResourcePath(manifest.glance) ||
       !isSafeResourcePath(manifest.icon_pack)) {
        error = ThemeValidationError::UnsafeResourcePath;
        return false;
    }
    if(!endsWith(manifest.wallpaper, ".rgb565") ||
       !endsWith(manifest.glance, ".png") || strchr(manifest.icon_pack, '.') != nullptr) {
        error = ThemeValidationError::UnsupportedResource;
        return false;
    }
    return true;
}

bool ThemePackageService::importPackage(fs::FS & filesystem,
                                        const char * theme_root,
                                        StorageService & storage,
                                        ThemeValidationError * error_out) const {
    ThemeValidationError error = ThemeValidationError::None;
    char manifest_path[192];
    if(!theme_root || snprintf(manifest_path, sizeof(manifest_path),
                              "%s/theme.json", theme_root) <= 0) {
        error = ThemeValidationError::UnsafeResourcePath;
    }

    ThemeManifest manifest{};
    fs::File file;
    char json[kMaxManifestBytes + 1]{};
    if(error == ThemeValidationError::None) {
        file = filesystem.open(manifest_path, FILE_READ);
        if(!file || file.isDirectory()) {
            error = ThemeValidationError::ManifestMissing;
        } else if(file.size() == 0 || file.size() > kMaxManifestBytes) {
            error = ThemeValidationError::ManifestTooLarge;
        } else {
            const size_t size = file.size();
            if(file.read(reinterpret_cast<uint8_t *>(json), size) != size) {
                error = ThemeValidationError::MalformedJson;
            } else {
                json[size] = '\0';
                parseManifest(json, size, manifest, error);
            }
        }
        if(file) file.close();
    }

    if(error == ThemeValidationError::None &&
       !validThemeRoot(theme_root, manifest.id)) {
        error = ThemeValidationError::UnsafeResourcePath;
    }

    char wallpaper_path[224];
    char glance_path[224];
    char icons_path[224];
    if(error == ThemeValidationError::None &&
       (!joinPath(theme_root, manifest.wallpaper, wallpaper_path,
                  sizeof(wallpaper_path)) ||
        !joinPath(theme_root, manifest.glance, glance_path,
                  sizeof(glance_path)) ||
        !joinPath(theme_root, manifest.icon_pack, icons_path,
                  sizeof(icons_path)))) {
        error = ThemeValidationError::UnsafeResourcePath;
    }
    if(error == ThemeValidationError::None &&
       !validateWallpaper(filesystem, wallpaper_path)) {
        error = ThemeValidationError::ResourceInvalid;
    }
    if(error == ThemeValidationError::None &&
       !validatePng(filesystem, glance_path)) {
        error = ThemeValidationError::ResourceInvalid;
    }
    if(error == ThemeValidationError::None &&
       !validateIconPack(filesystem, icons_path)) {
        error = ThemeValidationError::ResourceInvalid;
    }

    SystemSettings settings{};
    if(error == ThemeValidationError::None && !storage.loadSettings(settings)) {
        error = ThemeValidationError::StorageUnavailable;
    }
    if(error == ThemeValidationError::None &&
       !storage.saveThemeCache(manifest.id, manifest.palette)) {
        error = ThemeValidationError::StorageUnavailable;
    }
    if(error == ThemeValidationError::None) {
        strlcpy(settings.theme_id, manifest.id, sizeof(settings.theme_id));
        if(!storage.saveSettings(settings)) {
            error = ThemeValidationError::StorageUnavailable;
        }
    }
    if(error_out) *error_out = error;
    return error == ThemeValidationError::None;
}

bool ThemePackageService::isSafeResourcePath(const char * path) {
    if(!path || !path[0] || path[0] == '/' || strlen(path) >= 64) return false;
    const char * component = path;
    for(const char * cursor = path; ; ++cursor) {
        const char value = *cursor;
        if(value == '\\' || value == ':' ||
           (static_cast<unsigned char>(value) < 0x20 && value != '\0')) {
            return false;
        }
        if(value == '/' || value == '\0') {
            const size_t size = static_cast<size_t>(cursor - component);
            if(size == 0 || (size == 1 && component[0] == '.') ||
               (size == 2 && component[0] == '.' && component[1] == '.')) {
                return false;
            }
            if(value == '\0') break;
            component = cursor + 1;
        }
    }
    return true;
}

const ThemeManifest & ThemePackageService::neutralDefault() {
    static const uint32_t palette[5] = {
        0x080B10, 0x151A22, 0xD8E0EA, 0x8E9AAA, 0xFF5A5F
    };
    static const ThemeManifest manifest = makeBuiltIn(
        "neutral-default", "Neutral", palette);
    return manifest;
}

const ThemeManifest & ThemePackageService::fireflyDefault() {
#if FIREFLYOS_INCLUDE_FIREFLY_THEME
    static const uint32_t palette[5] = {
        0x05090C, 0x0C1820, 0x5FE7C7, 0x6EC4D6, 0xFF5A5F
    };
    static const ThemeManifest manifest = makeBuiltIn(
        "firefly-default", "Firefly", palette);
    return manifest;
#else
    return neutralDefault();
#endif
}

}  // namespace firefly
