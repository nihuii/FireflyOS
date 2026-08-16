#include "UpdateSources.h"

#include "UpdateReleaseConfig.h"

#include <WiFi.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace firefly {
namespace {

constexpr char kManagedUpdatePrefix[] = "/FireflyOS/Updates/";
constexpr uint32_t kHeaderTimeoutMs = 15000;
constexpr uint32_t kReadStallTimeoutMs = 15000;
constexpr uint32_t kOverallTimeoutMs = 10UL * 60UL * 1000UL;
constexpr size_t kMaxHeaderBytes = 2048;

bool deadlineOpen(uint32_t started_ms, uint32_t timeout_ms) {
    return static_cast<uint32_t>(millis() - started_ms) < timeout_ms;
}

#if FIREFLY_UPDATE_CONFIGURED
bool parseContentLength(const char * value, uint32_t & output) {
    while(*value == ' ' || *value == '\t') ++value;
    if(*value < '0' || *value > '9') return false;
    uint32_t parsed = 0;
    while(*value >= '0' && *value <= '9') {
        const uint32_t digit = static_cast<uint32_t>(*value - '0');
        if(parsed > (UINT32_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
        ++value;
    }
    while(*value == ' ' || *value == '\t') ++value;
    if(*value != '\0') return false;
    output = parsed;
    return true;
}

bool beginsCaseInsensitive(const char * value, const char * prefix) {
    while(*prefix) {
        if(tolower(static_cast<unsigned char>(*value++)) !=
           tolower(static_cast<unsigned char>(*prefix++))) return false;
    }
    return true;
}
#endif

}  // namespace

SdManifestSource::SdManifestSource(StorageService & storage,
                                   const char * managed_path)
    : storage_(storage) {
    if(managed_path && strnlen(managed_path, sizeof(path_)) < sizeof(path_)) {
        strlcpy(path_, managed_path, sizeof(path_));
    }
}

UpdateIoResult SdManifestSource::fetch(UpdateManifest & output) {
    output = {};
    if(path_[0] == '\0') return UpdateIoResult::Unavailable;
    fs::File file = storage_.openManaged(path_, FILE_READ);
    if(!file || file.isDirectory() || file.size() == 0) {
        storage_.closeManaged(file);
        return UpdateIoResult::Unavailable;
    }
    if(file.size() > UpdateManifestCodec::kMaxJsonBytes) {
        storage_.closeManaged(file);
        return UpdateIoResult::Error;
    }
    char json[UpdateManifestCodec::kMaxJsonBytes + 1]{};
    const size_t expected = file.size();
    const size_t count = storage_.readManaged(
        file, reinterpret_cast<uint8_t *>(json), expected);
    storage_.closeManaged(file);
    if(count != expected ||
       !UpdateManifestCodec::parseJson(json, count, output)) {
        output = {};
        return UpdateIoResult::Error;
    }
    return UpdateIoResult::Ok;
}

SdUpdateSource::SdUpdateSource(StorageService & storage,
                               const char * managed_path)
    : storage_(storage) {
    if(validManagedUpdatePath(managed_path)) {
        strlcpy(path_, managed_path, sizeof(path_));
    }
}

bool SdUpdateSource::validManagedUpdatePath(const char * path) {
    if(!path) return false;
    const size_t prefix_length = sizeof(kManagedUpdatePrefix) - 1;
    if(strncmp(path, kManagedUpdatePrefix, prefix_length) != 0) return false;
    const char * name = path + prefix_length;
    const size_t length = strlen(name);
    if(length < 5 || length > 64 || strcmp(name + length - 4, ".bin") != 0 ||
       strstr(name, "..") != nullptr ||
       strstr(name, ".part") != nullptr) return false;
    for(size_t index = 0; index < length; ++index) {
        const unsigned char value = static_cast<unsigned char>(name[index]);
        if(!(isalnum(value) || value == '-' || value == '_' || value == '.')) {
            return false;
        }
    }
    return true;
}

UpdateIoResult SdUpdateSource::open(const UpdateManifest & manifest) {
    close();
    if(path_[0] == '\0' || !storage_.beginOtaSdSession()) {
        return UpdateIoResult::Unavailable;
    }
    session_open_ = true;
    file_ = storage_.openOtaManaged(path_);
    if(!file_ || file_.isDirectory() || file_.size() != manifest.size) {
        close();
        return UpdateIoResult::Unavailable;
    }
    expected_size_ = manifest.size;
    return UpdateIoResult::Ok;
}

UpdateIoResult SdUpdateSource::read(uint8_t * output,
                                    size_t capacity,
                                    size_t & output_length) {
    output_length = 0;
    if(!session_open_ || !file_ || !output || capacity == 0 ||
       capacity > UpdateService::kChunkBytes || !storage_.otaSdAvailable()) {
        return UpdateIoResult::Unavailable;
    }
    if(file_.available() == 0) return UpdateIoResult::End;
    output_length = storage_.readOtaManaged(file_, output, capacity);
    return output_length > 0 ? UpdateIoResult::Ok
                             : UpdateIoResult::Unavailable;
}

void SdUpdateSource::close() {
    if(file_) storage_.closeOtaManaged(file_);
    if(session_open_) storage_.endOtaSdSession();
    session_open_ = false;
    expected_size_ = 0;
}

HttpsUpdateSource::HttpsUpdateSource(const char * filename) {
    if(validFilename(filename)) strlcpy(filename_, filename, sizeof(filename_));
}

bool HttpsUpdateSource::configured() {
#if FIREFLY_UPDATE_CONFIGURED
    return true;
#else
    return false;
#endif
}

bool HttpsUpdateSource::validFilename(const char * filename) {
    if(!filename) return false;
    const size_t length = strlen(filename);
    if(length < 5 || length > 64 || strcmp(filename + length - 4, ".bin") != 0 ||
       strstr(filename, "..") != nullptr ||
       strstr(filename, ".part") != nullptr) return false;
    for(size_t index = 0; index < length; ++index) {
        const unsigned char value = static_cast<unsigned char>(filename[index]);
        if(!(isalnum(value) || value == '-' || value == '_' || value == '.')) {
            return false;
        }
    }
    return true;
}

UpdateIoResult HttpsUpdateSource::open(const UpdateManifest & manifest) {
    close();
    if(!configured() || filename_[0] == '\0') return UpdateIoResult::NoEndpoint;
#if FIREFLY_UPDATE_CONFIGURED
    constexpr char base[] = FIREFLY_UPDATE_BASE_URL;
    constexpr char host[] = FIREFLY_UPDATE_HOST;
    constexpr char scheme[] = "https://";
    const size_t scheme_length = sizeof(scheme) - 1;
    const size_t host_length = sizeof(host) - 1;
    if(strncmp(base, scheme, scheme_length) != 0 ||
       strncmp(base + scheme_length, host, host_length) != 0 ||
       (base[scheme_length + host_length] != '\0' &&
        base[scheme_length + host_length] != '/')) {
        return UpdateIoResult::NoEndpoint;
    }
    const char * base_path = base + scheme_length + host_length;
    char request_path[192]{};
    const int path_length = snprintf(
        request_path, sizeof(request_path), "%s%s%s",
        base_path[0] ? base_path : "/",
        base_path[0] && base_path[strlen(base_path) - 1] == '/' ? "" : "/",
        filename_);
    if(path_length <= 0 || static_cast<size_t>(path_length) >=
       sizeof(request_path) || strchr(request_path, '\r') ||
       strchr(request_path, '\n')) return UpdateIoResult::NoEndpoint;

    opened_ms_ = millis();
    last_read_ms_ = opened_ms_;
    IPAddress address;
    if(WiFi.hostByName(host, address) != 1 ||
       !deadlineOpen(opened_ms_, kHeaderTimeoutMs)) {
        return UpdateIoResult::Timeout;
    }
    client_.setTimeout(5);
    client_.setHandshakeTimeout(5);
    if(!client_.connect(address, 443, host, FIREFLY_UPDATE_CA_CERT,
                        nullptr, nullptr) ||
       !deadlineOpen(opened_ms_, kHeaderTimeoutMs)) {
        close();
        return UpdateIoResult::Unavailable;
    }
    char request[320]{};
    const int request_length = snprintf(
        request, sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: application/octet-stream\r\n"
        "Accept-Encoding: identity\r\nConnection: close\r\n\r\n",
        request_path, host);
    if(request_length <= 0 || static_cast<size_t>(request_length) >=
       sizeof(request) ||
       client_.write(reinterpret_cast<const uint8_t *>(request),
                     static_cast<size_t>(request_length)) !=
           static_cast<size_t>(request_length)) {
        close();
        return UpdateIoResult::Unavailable;
    }

    size_t total_header_bytes = 0;
    char line[256]{};
    UpdateIoResult line_result = readLine(line, sizeof(line), total_header_bytes);
    if(line_result != UpdateIoResult::Ok ||
       (strcmp(line, "HTTP/1.1 200 OK") != 0 &&
        strcmp(line, "HTTP/1.0 200 OK") != 0)) {
        close();
        return line_result == UpdateIoResult::Ok
            ? UpdateIoResult::Unavailable : line_result;
    }
    bool has_length = false;
    uint32_t content_length = 0;
    while(true) {
        line_result = readLine(line, sizeof(line), total_header_bytes);
        if(line_result != UpdateIoResult::Ok) {
            close();
            return line_result;
        }
        if(line[0] == '\0') break;
        if(beginsCaseInsensitive(line, "Content-Length:")) {
            if(has_length || !parseContentLength(line + 15, content_length)) {
                close();
                return UpdateIoResult::Unavailable;
            }
            has_length = true;
        } else if(beginsCaseInsensitive(line, "Transfer-Encoding:") ||
                  beginsCaseInsensitive(line, "Location:")) {
            close();
            return UpdateIoResult::Unavailable;
        } else if(beginsCaseInsensitive(line, "Content-Encoding:") &&
                  !beginsCaseInsensitive(line + 17, " identity")) {
            close();
            return UpdateIoResult::Unavailable;
        }
    }
    if(!has_length || content_length != manifest.size) {
        close();
        return UpdateIoResult::Unavailable;
    }
    expected_size_ = content_length;
    received_size_ = 0;
    last_read_ms_ = millis();
    open_ = true;
    return UpdateIoResult::Ok;
#else
    (void)manifest;
    return UpdateIoResult::NoEndpoint;
#endif
}

UpdateIoResult HttpsUpdateSource::readLine(char * output, size_t capacity,
                                           size_t & total_header_bytes) {
    if(!output || capacity < 2) return UpdateIoResult::Error;
    size_t length = 0;
    while(deadlineOpen(opened_ms_, kHeaderTimeoutMs)) {
        if(client_.available() <= 0) {
            if(!client_.connected()) return UpdateIoResult::Unavailable;
            delay(1);
            continue;
        }
        const int value = client_.read();
        if(value < 0) continue;
        if(++total_header_bytes > kMaxHeaderBytes) return UpdateIoResult::Error;
        if(value == '\n') {
            if(length > 0 && output[length - 1] == '\r') --length;
            output[length] = '\0';
            return UpdateIoResult::Ok;
        }
        if(length + 1 >= capacity) return UpdateIoResult::Error;
        output[length++] = static_cast<char>(value);
    }
    return UpdateIoResult::Timeout;
}

UpdateIoResult HttpsUpdateSource::read(uint8_t * output,
                                       size_t capacity,
                                       size_t & output_length) {
    output_length = 0;
    if(!open_ || !output || capacity == 0 ||
       capacity > UpdateService::kChunkBytes) return UpdateIoResult::Unavailable;
    while(client_.available() <= 0) {
        if(!client_.connected()) return UpdateIoResult::End;
        if(!deadlineOpen(opened_ms_, kOverallTimeoutMs) ||
           !deadlineOpen(last_read_ms_, kReadStallTimeoutMs)) {
            return UpdateIoResult::Timeout;
        }
        delay(1);
    }
    const size_t available = static_cast<size_t>(client_.available());
    const size_t requested = available < capacity ? available : capacity;
    const int count = client_.read(output, requested);
    if(count <= 0) return UpdateIoResult::Unavailable;
    output_length = static_cast<size_t>(count);
    received_size_ += static_cast<uint32_t>(output_length);
    last_read_ms_ = millis();
    return UpdateIoResult::Ok;
}

void HttpsUpdateSource::close() {
    client_.stop();
    expected_size_ = 0;
    received_size_ = 0;
    open_ = false;
}

HttpsManifestSource::HttpsManifestSource(const char * filename) {
    if(validFilename(filename)) strlcpy(filename_, filename, sizeof(filename_));
}

bool HttpsManifestSource::configured() {
    return HttpsUpdateSource::configured();
}

bool HttpsManifestSource::validFilename(const char * filename) {
    if(!filename) return false;
    const size_t length = strlen(filename);
    if(length < 6 || length > 64 ||
       strcmp(filename + length - 5, ".json") != 0 ||
       strstr(filename, "..") != nullptr ||
       strstr(filename, ".part") != nullptr) return false;
    for(size_t index = 0; index < length; ++index) {
        const unsigned char value = static_cast<unsigned char>(filename[index]);
        if(!(isalnum(value) || value == '-' || value == '_' || value == '.')) {
            return false;
        }
    }
    return true;
}

UpdateIoResult HttpsManifestSource::readLine(char * output, size_t capacity,
                                              size_t & total_header_bytes,
                                              uint32_t opened_ms) {
    if(!output || capacity < 2) return UpdateIoResult::Error;
    size_t length = 0;
    while(deadlineOpen(opened_ms, kHeaderTimeoutMs)) {
        if(client_.available() <= 0) {
            if(!client_.connected()) return UpdateIoResult::Unavailable;
            delay(1);
            continue;
        }
        const int value = client_.read();
        if(value < 0) continue;
        if(++total_header_bytes > kMaxHeaderBytes) return UpdateIoResult::Error;
        if(value == '\n') {
            if(length > 0 && output[length - 1] == '\r') --length;
            output[length] = '\0';
            return UpdateIoResult::Ok;
        }
        if(length + 1 >= capacity) return UpdateIoResult::Error;
        output[length++] = static_cast<char>(value);
    }
    return UpdateIoResult::Timeout;
}

UpdateIoResult HttpsManifestSource::fetch(UpdateManifest & output) {
    output = {};
    client_.stop();
    if(!configured() || filename_[0] == '\0') return UpdateIoResult::NoEndpoint;
#if FIREFLY_UPDATE_CONFIGURED
    constexpr char base[] = FIREFLY_UPDATE_BASE_URL;
    constexpr char host[] = FIREFLY_UPDATE_HOST;
    constexpr char scheme[] = "https://";
    const size_t scheme_length = sizeof(scheme) - 1;
    const size_t host_length = sizeof(host) - 1;
    if(host_length == 0 || strncmp(base, scheme, scheme_length) != 0 ||
       strncmp(base + scheme_length, host, host_length) != 0 ||
       (base[scheme_length + host_length] != '\0' &&
        base[scheme_length + host_length] != '/')) {
        return UpdateIoResult::NoEndpoint;
    }
    const char * base_path = base + scheme_length + host_length;
    char request_path[192]{};
    const int path_length = snprintf(
        request_path, sizeof(request_path), "%s%s%s",
        base_path[0] ? base_path : "/",
        base_path[0] && base_path[strlen(base_path) - 1] == '/' ? "" : "/",
        filename_);
    if(path_length <= 0 || static_cast<size_t>(path_length) >=
       sizeof(request_path)) return UpdateIoResult::NoEndpoint;

    const uint32_t opened_ms = millis();
    IPAddress address;
    if(WiFi.hostByName(host, address) != 1 ||
       !deadlineOpen(opened_ms, kHeaderTimeoutMs)) {
        return UpdateIoResult::Timeout;
    }
    client_.setTimeout(5);
    client_.setHandshakeTimeout(5);
    if(!client_.connect(address, 443, host, FIREFLY_UPDATE_CA_CERT,
                        nullptr, nullptr) ||
       !deadlineOpen(opened_ms, kHeaderTimeoutMs)) {
        client_.stop();
        return UpdateIoResult::Unavailable;
    }
    char request[320]{};
    const int request_length = snprintf(
        request, sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: application/json\r\n"
        "Accept-Encoding: identity\r\nConnection: close\r\n\r\n",
        request_path, host);
    if(request_length <= 0 || static_cast<size_t>(request_length) >=
       sizeof(request) ||
       client_.write(reinterpret_cast<const uint8_t *>(request),
                     static_cast<size_t>(request_length)) !=
           static_cast<size_t>(request_length)) {
        client_.stop();
        return UpdateIoResult::Unavailable;
    }

    size_t header_bytes = 0;
    char line[256]{};
    UpdateIoResult result = readLine(line, sizeof(line), header_bytes, opened_ms);
    if(result != UpdateIoResult::Ok ||
       (strcmp(line, "HTTP/1.1 200 OK") != 0 &&
        strcmp(line, "HTTP/1.0 200 OK") != 0)) {
        client_.stop();
        return result == UpdateIoResult::Ok ? UpdateIoResult::Unavailable
                                            : result;
    }
    bool has_length = false;
    uint32_t content_length = 0;
    while(true) {
        result = readLine(line, sizeof(line), header_bytes, opened_ms);
        if(result != UpdateIoResult::Ok) {
            client_.stop();
            return result;
        }
        if(line[0] == '\0') break;
        if(beginsCaseInsensitive(line, "Content-Length:")) {
            if(has_length || !parseContentLength(line + 15, content_length)) {
                client_.stop();
                return UpdateIoResult::Error;
            }
            has_length = true;
        } else if(beginsCaseInsensitive(line, "Transfer-Encoding:") ||
                  beginsCaseInsensitive(line, "Location:")) {
            client_.stop();
            return UpdateIoResult::Error;
        } else if(beginsCaseInsensitive(line, "Content-Encoding:") &&
                  !beginsCaseInsensitive(line + 17, " identity")) {
            client_.stop();
            return UpdateIoResult::Error;
        }
    }
    if(!has_length || content_length == 0 ||
       content_length > UpdateManifestCodec::kMaxJsonBytes) {
        client_.stop();
        return UpdateIoResult::Error;
    }

    char json[UpdateManifestCodec::kMaxJsonBytes + 1]{};
    size_t received = 0;
    uint32_t last_read_ms = millis();
    while(received < content_length) {
        if(client_.available() <= 0) {
            if(!client_.connected() ||
               !deadlineOpen(last_read_ms, kReadStallTimeoutMs)) {
                client_.stop();
                return UpdateIoResult::Timeout;
            }
            delay(1);
            continue;
        }
        const size_t remaining = content_length - received;
        const size_t available = static_cast<size_t>(client_.available());
        const size_t requested = available < remaining ? available : remaining;
        const int count = client_.read(
            reinterpret_cast<uint8_t *>(json) + received, requested);
        if(count <= 0) {
            client_.stop();
            return UpdateIoResult::Unavailable;
        }
        received += static_cast<size_t>(count);
        last_read_ms = millis();
    }
    client_.stop();
    if(!UpdateManifestCodec::parseJson(json, received, output)) {
        output = {};
        return UpdateIoResult::Error;
    }
    return UpdateIoResult::Ok;
#else
    return UpdateIoResult::NoEndpoint;
#endif
}

}  // namespace firefly
