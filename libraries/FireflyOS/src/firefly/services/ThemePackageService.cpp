#include "ThemePackageService.h"

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

uint32_t boundedIssueValue(uint64_t value) {
    return value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
}

void setIssue(ThemeValidationIssue & issue,
              ThemeValidationError error,
              const char * resource,
              uint32_t actual = 0,
              uint32_t limit = 0) {
    issue = ThemeValidationIssue{};
    issue.error = error;
    if(resource) strlcpy(issue.resource, resource, sizeof(issue.resource));
    issue.actual = actual;
    issue.limit = limit;
}

bool validateWallpaper(StorageService & storage,
                       const char * path,
                       const ThemeManifest & manifest,
                       ThemeValidationIssue & issue) {
    fs::File file = storage.openManaged(path, FILE_READ);
    if(!file) {
        setIssue(issue, ThemeValidationError::ResourceMissing,
                 manifest.wallpaper);
        return false;
    }
    bool is_directory = false;
    uint64_t file_size = 0;
    if(!storage.managedFileIsDirectory(file, is_directory) || is_directory ||
       !storage.managedFileSize(file, file_size)) {
        if(file) storage.closeManaged(file);
        setIssue(issue, ThemeValidationError::ResourceMissing,
                 manifest.wallpaper);
        return false;
    }
    const uint64_t size = file_size;
    const uint32_t expected_wallpaper_bytes =
        static_cast<uint32_t>(manifest.wallpaper_width) *
        manifest.wallpaper_height * sizeof(uint16_t);
    uint8_t sample[2]{};
    const bool valid = size == expected_wallpaper_bytes &&
        storage.readManaged(file, sample, sizeof(sample)) == sizeof(sample);
    storage.closeManaged(file);
    if(!valid) {
        setIssue(issue, size > expected_wallpaper_bytes
                 ? ThemeValidationError::ResourceTooLarge
                 : ThemeValidationError::ResourceInvalid,
                 manifest.wallpaper, boundedIssueValue(size),
                 expected_wallpaper_bytes);
    }
    return valid;
}

bool validatePng(StorageService & storage,
                 const char * path,
                 const char * resource,
                 ThemeValidationIssue & issue) {
    fs::File file = storage.openManaged(path, FILE_READ);
    if(!file) {
        setIssue(issue, ThemeValidationError::ResourceMissing, resource);
        return false;
    }
    bool is_directory = false;
    uint64_t file_size = 0;
    if(!storage.managedFileIsDirectory(file, is_directory) || is_directory ||
       !storage.managedFileSize(file, file_size)) {
        if(file) storage.closeManaged(file);
        setIssue(issue, ThemeValidationError::ResourceMissing, resource);
        return false;
    }
    if(file_size > ThemePackageService::kMaxGlanceBytes) {
        storage.closeManaged(file);
        setIssue(issue, ThemeValidationError::ResourceTooLarge, resource,
                 boundedIssueValue(file_size),
                 ThemePackageService::kMaxGlanceBytes);
        return false;
    }
    uint8_t header[24]{};
    const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    const bool read = storage.readManaged(file, header, sizeof(header)) == sizeof(header);
    storage.closeManaged(file);
    if(!read || memcmp(header, signature, sizeof(signature)) != 0 ||
       memcmp(header + 12, "IHDR", 4) != 0) {
        setIssue(issue, ThemeValidationError::ResourceInvalid, resource);
        return false;
    }
    const uint32_t width = readBigEndian32(header + 16);
    const uint32_t height = readBigEndian32(header + 20);
    if(width == 0 || height == 0) {
        setIssue(issue, ThemeValidationError::ResourceInvalid, resource);
        return false;
    }
    if(width > 410) {
        setIssue(issue, ThemeValidationError::ResourceTooLarge, resource,
                 width, 410);
        return false;
    }
    if(height > 502) {
        setIssue(issue, ThemeValidationError::ResourceTooLarge, resource,
                 height, 502);
        return false;
    }
    return true;
}

bool validateIconPack(StorageService & storage,
                      const char * path,
                      const char * resource,
                      ThemeValidationIssue & issue) {
    fs::File directory = storage.openManaged(path, FILE_READ);
    if(!directory) {
        setIssue(issue, ThemeValidationError::ResourceMissing, resource);
        return false;
    }
    bool is_directory = false;
    if(!storage.managedFileIsDirectory(directory, is_directory) || !is_directory) {
        if(directory) storage.closeManaged(directory);
        setIssue(issue, ThemeValidationError::ResourceMissing, resource);
        return false;
    }
    uint64_t total = 0;
    uint16_t count = 0;
    fs::File entry = storage.openNextManaged(directory);
    while(entry) {
        char entry_name[64]{};
        uint64_t entry_size = 0;
        bool entry_is_directory = false;
        const bool metadata_ok =
            storage.managedFileName(entry, entry_name, sizeof(entry_name)) &&
            storage.managedFileSize(entry, entry_size) &&
            storage.managedFileIsDirectory(entry, entry_is_directory);
        if(!metadata_ok || entry_is_directory ||
           (!endsWith(entry_name, ".png") &&
            !endsWith(entry_name, ".rgb565"))) {
            storage.closeManaged(entry);
            storage.closeManaged(directory);
            setIssue(issue, ThemeValidationError::UnsupportedResource,
                     metadata_ok ? entry_name : resource);
            return false;
        }
        total += entry_size;
        ++count;
        storage.closeManaged(entry);
        if(count > ThemePackageService::kMaxIconFiles) {
            storage.closeManaged(directory);
            setIssue(issue, ThemeValidationError::ResourceTooLarge, resource,
                     count, ThemePackageService::kMaxIconFiles);
            return false;
        }
        if(total > ThemePackageService::kMaxIconPackBytes) {
            storage.closeManaged(directory);
            setIssue(issue, ThemeValidationError::ResourceTooLarge, resource,
                     boundedIssueValue(total),
                     ThemePackageService::kMaxIconPackBytes);
            return false;
        }
        entry = storage.openNextManaged(directory);
    }
    storage.closeManaged(directory);
    if(count == 0) {
        setIssue(issue, ThemeValidationError::ResourceMissing, resource);
        return false;
    }
    return true;
}

ThemeManifest makeBuiltIn(const char * id,
                          const char * name,
                          const uint32_t palette[5]) {
    ThemeManifest manifest{};
    strlcpy(manifest.id, id, sizeof(manifest.id));
    strlcpy(manifest.name, name, sizeof(manifest.name));
    strlcpy(manifest.author, "FireflyOS", sizeof(manifest.author));
    memcpy(manifest.palette, palette, sizeof(manifest.palette));
    manifest.wallpaper_width = 410;
    manifest.wallpaper_height = 502;
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
    uint32_t wallpaper_width = 0;
    uint32_t wallpaper_height = 0;
    if(!readUnsigned(bounded, "wallpaper_width", wallpaper_width) ||
       !readUnsigned(bounded, "wallpaper_height", wallpaper_height) ||
       wallpaper_width == 0 || wallpaper_height == 0) {
        error = ThemeValidationError::ResourceInvalid;
        return false;
    }
    if(wallpaper_width > 410 || wallpaper_height > 502) {
        error = ThemeValidationError::ResourceTooLarge;
        return false;
    }
    manifest.wallpaper_width = static_cast<uint16_t>(wallpaper_width);
    manifest.wallpaper_height = static_cast<uint16_t>(wallpaper_height);
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

bool ThemePackageService::validatePackage(StorageService & storage,
                                          const char * theme_root,
                                          ThemeManifest & manifest,
                                          ThemeValidationIssue & issue) const {
    manifest = ThemeManifest{};
    issue = ThemeValidationIssue{};
    char manifest_path[192];
    if(!theme_root || snprintf(manifest_path, sizeof(manifest_path),
                              "%s/theme.json", theme_root) <= 0) {
        setIssue(issue, ThemeValidationError::UnsafeResourcePath, "theme.json");
        return false;
    }

    fs::File file;
    char json[kMaxManifestBytes + 1]{};
    file = storage.openManaged(manifest_path, FILE_READ);
    if(!file) {
        setIssue(issue, ThemeValidationError::ManifestMissing, "theme.json");
        return false;
    }
    bool is_directory = false;
    uint64_t file_size = 0;
    if(!storage.managedFileIsDirectory(file, is_directory) || is_directory ||
       !storage.managedFileSize(file, file_size)) {
        if(file) storage.closeManaged(file);
        setIssue(issue, ThemeValidationError::ManifestMissing, "theme.json");
        return false;
    }
    if(file_size == 0 || file_size > kMaxManifestBytes) {
        storage.closeManaged(file);
        setIssue(issue, ThemeValidationError::ManifestTooLarge, "theme.json",
                 boundedIssueValue(file_size), kMaxManifestBytes);
        return false;
    }
    const size_t manifest_size = static_cast<size_t>(file_size);
    const bool read = storage.readManaged(file,
        reinterpret_cast<uint8_t *>(json), manifest_size) == manifest_size;
    storage.closeManaged(file);
    ThemeValidationError parse_error = ThemeValidationError::None;
    if(!read) parse_error = ThemeValidationError::MalformedJson;
    else {
        json[manifest_size] = '\0';
        parseManifest(json, manifest_size, manifest, parse_error);
    }
    if(parse_error != ThemeValidationError::None) {
        if(parse_error == ThemeValidationError::ResourceTooLarge) {
            uint32_t width = 0;
            uint32_t height = 0;
            readUnsigned(json, "wallpaper_width", width);
            readUnsigned(json, "wallpaper_height", height);
            const bool width_exceeded = width > 410;
            setIssue(issue, parse_error,
                     manifest.wallpaper[0] ? manifest.wallpaper : "wallpaper.rgb565",
                     width_exceeded ? width : height,
                     width_exceeded ? 410 : 502);
        } else {
            setIssue(issue, parse_error, "theme.json");
        }
        return false;
    }
    if(!validThemeRoot(theme_root, manifest.id)) {
        setIssue(issue, ThemeValidationError::UnsafeResourcePath, "theme.json");
        return false;
    }

    char wallpaper_path[224];
    char glance_path[224];
    char icons_path[224];
    if(!joinPath(theme_root, manifest.wallpaper, wallpaper_path,
                 sizeof(wallpaper_path)) ||
        !joinPath(theme_root, manifest.glance, glance_path,
                  sizeof(glance_path)) ||
        !joinPath(theme_root, manifest.icon_pack, icons_path,
                  sizeof(icons_path))) {
        setIssue(issue, ThemeValidationError::UnsafeResourcePath, "theme.json");
        return false;
    }
    return validateWallpaper(storage, wallpaper_path, manifest, issue) &&
           validatePng(storage, glance_path, manifest.glance, issue) &&
           validateIconPack(storage, icons_path, manifest.icon_pack, issue);
}

bool ThemePackageService::importPackage(StorageService & storage,
                                        const char * theme_root,
                                        ThemeValidationIssue * issue_out) const {
    ThemeManifest manifest{};
    ThemeValidationIssue issue{};
    if(!validatePackage(storage, theme_root, manifest, issue)) {
        if(issue_out) *issue_out = issue;
        return false;
    }

    SystemSettings settings{};
    char previous_cache_id[24]{};
    uint32_t previous_palette[5]{};
    bool previous_cache_present = false;
    if(!storage.loadSettings(settings) ||
       !storage.loadThemeCache(previous_cache_id, sizeof(previous_cache_id),
                               previous_palette, previous_cache_present)) {
        setIssue(issue, ThemeValidationError::StorageUnavailable, "settings");
        if(issue_out) *issue_out = issue;
        return false;
    }
    if(!storage.saveThemeCache(manifest.id, manifest.palette)) {
        setIssue(issue, ThemeValidationError::StorageUnavailable, "theme-cache");
        if(issue_out) *issue_out = issue;
        return false;
    }
    SystemSettings updated = settings;
    strlcpy(updated.theme_id, manifest.id, sizeof(updated.theme_id));
    if(!storage.saveSettings(updated)) {
        if(previous_cache_present) {
            storage.saveThemeCache(previous_cache_id, previous_palette);
        } else {
            storage.clearThemeCache();
        }
        storage.saveSettings(settings);
        setIssue(issue, ThemeValidationError::StorageUnavailable, "settings");
        if(issue_out) *issue_out = issue;
        return false;
    }
    if(issue_out) *issue_out = ThemeValidationIssue{};
    return true;
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

const char * ThemePackageService::errorText(ThemeValidationError error) {
    switch(error) {
        case ThemeValidationError::ManifestMissing: return "theme.json missing";
        case ThemeValidationError::ManifestTooLarge: return "Manifest too large";
        case ThemeValidationError::MalformedJson: return "Malformed theme.json";
        case ThemeValidationError::UnsupportedSchema: return "Unsupported schema";
        case ThemeValidationError::InvalidId: return "Invalid theme id";
        case ThemeValidationError::InvalidPalette: return "Invalid palette";
        case ThemeValidationError::UnsafeResourcePath: return "Unsafe resource path";
        case ThemeValidationError::UnsupportedResource: return "Unsupported resource";
        case ThemeValidationError::ResourceMissing: return "Resource missing";
        case ThemeValidationError::ResourceTooLarge: return "Resource too large";
        case ThemeValidationError::ResourceInvalid: return "Resource invalid";
        case ThemeValidationError::StorageUnavailable: return "Storage unavailable";
        default: return "Theme validation failed";
    }
}

const ThemeManifest & ThemePackageService::neutralDefault() {
    static const uint32_t palette[5] = {
        0x080B10, 0x151A22, 0xD8E0EA, 0x8E9AAA, 0xFF5A5F
    };
    static const ThemeManifest manifest = makeBuiltIn(
        "system-default", "Default", palette);
    return manifest;
}

const ThemeManifest & ThemePackageService::fireflyDefault() {
#if FIREFLYOS_INCLUDE_FIREFLY_THEME
    static const uint32_t palette[5] = {
        0x05090C, 0x0C1820, 0x5FE7C7, 0x6EC4D6, 0xFF5A5F
    };
    static const ThemeManifest manifest = makeBuiltIn(
        "firefly-accent", "Firefly", palette);
    return manifest;
#else
    return neutralDefault();
#endif
}

}  // namespace firefly
