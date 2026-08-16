#include "WeatherService.h"

#include <atomic>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include <lwip/dns.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace firefly {
namespace {

constexpr char kCachePath[] = "/weather.cache";
constexpr char kCachePartPath[] = "/weather.cache.part";
constexpr char kCacheBackupPath[] = "/weather.cache.bak";
constexpr char kLocationPath[] = "/weather.location";
constexpr char kLocationPartPath[] = "/weather.location.part";
constexpr uint32_t kCacheMagic = 0x31544857UL;
constexpr uint8_t kCacheVersion = 2;

struct WeatherCacheRecord {
    uint32_t magic = kCacheMagic;
    uint8_t version = kCacheVersion;
    uint8_t source = 0;
    uint16_t reserved = 0;
    uint32_t checksum = 0;
    WeatherSnapshot snapshot{};
};

struct WeatherLocationRecord {
    uint32_t magic = 0x31434F4CUL;
    uint8_t version = 1;
    uint8_t reserved[3]{};
    double latitude = 0.0;
    double longitude = 0.0;
    char city[32]{};
};

const char kIsrgRootX1[] PROGMEM = R"CERT(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)CERT";

uint32_t weatherRecordChecksum(const WeatherCacheRecord & record) {
    WeatherCacheRecord copy = record;
    copy.checksum = 0;
    uint32_t value = 2166136261UL;
    const uint8_t * bytes = reinterpret_cast<const uint8_t *>(&copy);
    for(size_t index = 0; index < sizeof(copy); ++index) {
        value ^= bytes[index];
        value *= 16777619UL;
    }
    return value;
}

bool loadWeatherRecord(const char * path, WeatherCacheRecord & record) {
    File file = LittleFS.open(path, FILE_READ);
    if(!file || file.size() != sizeof(WeatherCacheRecord)) return false;
    const bool ok = file.read(reinterpret_cast<uint8_t *>(&record),
                              sizeof(record)) == sizeof(record);
    file.close();
    return ok && record.magic == kCacheMagic &&
        record.version == kCacheVersion &&
        record.checksum == weatherRecordChecksum(record) &&
        record.source >= static_cast<uint8_t>(WeatherSource::Phone) &&
        record.source <= static_cast<uint8_t>(WeatherSource::Direct) &&
        record.snapshot.valid;
}

uint16_t readU16(const uint8_t * value) {
    return static_cast<uint16_t>(value[0]) |
        (static_cast<uint16_t>(value[1]) << 8);
}

int16_t readI16(const uint8_t * value) {
    return static_cast<int16_t>(readU16(value));
}

int64_t readI64(const uint8_t * value) {
    uint64_t raw = 0;
    for(uint8_t index = 0; index < 8; ++index) {
        raw |= static_cast<uint64_t>(value[index]) << (index * 8);
    }
    return static_cast<int64_t>(raw);
}

int32_t readI32(const uint8_t * value) {
    return static_cast<int32_t>(
        static_cast<uint32_t>(value[0]) |
        (static_cast<uint32_t>(value[1]) << 8) |
        (static_cast<uint32_t>(value[2]) << 16) |
        (static_cast<uint32_t>(value[3]) << 24));
}

bool copyBoundedText(char * output, size_t capacity,
                     const uint8_t * input, size_t length) {
    if(!output || !input || length == 0 || length >= capacity) return false;
    for(size_t index = 0; index < length; ++index) {
        if(input[index] < 0x20 || input[index] == 0x7F) return false;
    }
    memcpy(output, input, length);
    output[length] = '\0';
    return true;
}

size_t findToken(const char * json, size_t length, const char * token,
                 size_t offset = 0) {
    if(!json || !token || offset > length) return SIZE_MAX;
    const size_t token_length = strlen(token);
    if(token_length == 0 || token_length > length - offset) return SIZE_MAX;
    for(size_t index = offset; index + token_length <= length; ++index) {
        if(memcmp(json + index, token, token_length) == 0) return index;
    }
    return SIZE_MAX;
}

bool parseNumberInSection(const char * json, size_t length,
                          const char * section, const char * key,
                          double & output) {
    const size_t section_offset = findToken(json, length, section);
    if(section_offset == SIZE_MAX) return false;
    size_t object_start = section_offset + strlen(section);
    while(object_start < length &&
          (json[object_start] == ' ' || json[object_start] == '\t' ||
           json[object_start] == '\r' || json[object_start] == '\n')) {
        ++object_start;
    }
    if(object_start >= length || json[object_start++] != ':') return false;
    while(object_start < length &&
          (json[object_start] == ' ' || json[object_start] == '\t' ||
           json[object_start] == '\r' || json[object_start] == '\n')) {
        ++object_start;
    }
    if(object_start >= length || json[object_start] != '{') return false;
    size_t object_end = SIZE_MAX;
    uint16_t depth = 0;
    bool quoted = false;
    bool escaped = false;
    for(size_t index = object_start; index < length; ++index) {
        const char value = json[index];
        if(quoted) {
            if(escaped) escaped = false;
            else if(value == '\\') escaped = true;
            else if(value == '"') quoted = false;
            continue;
        }
        if(value == '"') quoted = true;
        else if(value == '{') ++depth;
        else if(value == '}' && --depth == 0) {
            object_end = index;
            break;
        }
    }
    if(object_end == SIZE_MAX) return false;
    size_t offset = findToken(json, object_end, key, object_start + 1);
    if(offset == SIZE_MAX) return false;
    offset += strlen(key);
    while(offset < length &&
          (json[offset] == ' ' || json[offset] == '\t' ||
           json[offset] == '\r' || json[offset] == '\n' ||
           json[offset] == '[')) ++offset;
    char number[32]{};
    size_t number_length = 0;
    while(offset < length && number_length + 1 < sizeof(number)) {
        const char value = json[offset];
        if((value < '0' || value > '9') && value != '-' && value != '+' &&
           value != '.' && value != 'e' && value != 'E') break;
        number[number_length++] = value;
        ++offset;
    }
    if(number_length == 0) return false;
    char * end = nullptr;
    const double value = strtod(number, &end);
    if(end != number + number_length || !isfinite(value)) return false;
    output = value;
    return true;
}

int16_t toTenths(double value, bool & ok) {
    const long rounded = lround(value * 10.0);
    ok = rounded >= -1000 && rounded <= 700;
    return ok ? static_cast<int16_t>(rounded) : 0;
}

uint32_t remainingWeatherBudget(uint32_t started_ms) {
    const uint32_t elapsed_ms = millis() - started_ms;
    return elapsed_ms >= WeatherService::kRequestTimeoutMs
        ? 0
        : WeatherService::kRequestTimeoutMs - elapsed_ms;
}

bool weatherDeadlineOpen(const WeatherDeadlineClock & clock,
                         uint32_t deadline_ms) {
    return static_cast<int32_t>(deadline_ms - clock.nowMs()) > 0;
}

bool readWeatherByte(WeatherResponseStream & stream,
                     WeatherDeadlineClock & clock,
                     uint32_t deadline_ms,
                     char & output) {
    while(weatherDeadlineOpen(clock, deadline_ms)) {
        if(stream.available() > 0) {
            const int value = stream.read();
            if(value >= 0) {
                output = static_cast<char>(value);
                return true;
            }
        } else if(!stream.connected()) {
            return false;
        }
        clock.idle();
    }
    return false;
}

bool readWeatherLine(WeatherResponseStream & stream,
                     WeatherDeadlineClock & clock,
                     uint32_t deadline_ms,
                     char * output,
                     size_t capacity,
                     size_t & length,
                     size_t & wire_bytes) {
    length = 0;
    if(!output || capacity < 2) return false;
    while(true) {
        char value = '\0';
        if(!readWeatherByte(stream, clock, deadline_ms, value)) return false;
        if(wire_bytes == SIZE_MAX) return false;
        ++wire_bytes;
        if(value == '\n') {
            output[length] = '\0';
            return true;
        }
        if(value == '\r') continue;
        if(length + 1 >= capacity) return false;
        output[length++] = value;
    }
}

bool asciiEqualIgnoreCase(char left, char right) {
    if(left >= 'A' && left <= 'Z') left = static_cast<char>(left + 32);
    if(right >= 'A' && right <= 'Z') right = static_cast<char>(right + 32);
    return left == right;
}

bool startsWithIgnoreCase(const char * value, const char * prefix) {
    if(!value || !prefix) return false;
    while(*prefix) {
        if(!*value || !asciiEqualIgnoreCase(*value, *prefix)) return false;
        ++value;
        ++prefix;
    }
    return true;
}

bool containsIgnoreCase(const char * value, const char * token) {
    if(!value || !token || !*token) return false;
    for(; *value; ++value) {
        const char * cursor = value;
        const char * expected = token;
        while(*cursor && *expected &&
              asciiEqualIgnoreCase(*cursor, *expected)) {
            ++cursor;
            ++expected;
        }
        if(!*expected) return true;
    }
    return false;
}

bool parseWeatherLength(const char * value, size_t & output) {
    output = 0;
    while(*value == ' ' || *value == '\t') ++value;
    if(*value < '0' || *value > '9') return false;
    while(*value >= '0' && *value <= '9') {
        const size_t digit = static_cast<size_t>(*value - '0');
        if(output > (SIZE_MAX - digit) / 10) return false;
        output = output * 10 + digit;
        ++value;
    }
    while(*value == ' ' || *value == '\t') ++value;
    return *value == '\0';
}

bool parseChunkLength(const char * value, size_t & output) {
    output = 0;
    bool any = false;
    while(*value == ' ' || *value == '\t') ++value;
    while(*value) {
        uint8_t digit = 0;
        if(*value >= '0' && *value <= '9') digit = *value - '0';
        else if(*value >= 'a' && *value <= 'f') digit = *value - 'a' + 10;
        else if(*value >= 'A' && *value <= 'F') digit = *value - 'A' + 10;
        else break;
        if(output > (SIZE_MAX - digit) / 16) return false;
        output = output * 16 + digit;
        any = true;
        ++value;
    }
    if(!any) return false;
    while(*value == ' ' || *value == '\t') ++value;
    return *value == '\0' || *value == ';';
}

bool appendWeatherByte(char value,
                       char * output,
                       size_t capacity,
                       size_t & output_length) {
    if(output_length >= capacity - 1) {
        output_length = capacity;
        output[capacity - 1] = '\0';
        return false;
    }
    output[output_length++] = value;
    output[output_length] = '\0';
    return true;
}

class EspWeatherResponseStream : public WeatherResponseStream {
public:
    explicit EspWeatherResponseStream(WiFiClientSecure & client)
        : client_(client) {}
    bool connected() override { return client_.connected(); }
    int available() override { return client_.available(); }
    int read() override { return client_.read(); }
private:
    WiFiClientSecure & client_;
};

class EspWeatherDeadlineClock : public WeatherDeadlineClock {
public:
    uint32_t nowMs() const override { return millis(); }
    void idle() override { delay(1); }
};

class EspWeatherDnsResolver {
public:
    bool resolve(const char * host,
                 WeatherDeadlineClock & clock,
                 uint32_t deadline_ms,
                 IPAddress & output) {
        bool expected = false;
        if(!host || !pending_.compare_exchange_strong(
               expected, true, std::memory_order_acq_rel)) return false;
        completed_.store(false, std::memory_order_relaxed);
        address_.store(0, std::memory_order_relaxed);
        ip_addr_t cached{};
        const err_t result = dns_gethostbyname(
            host, &cached, &EspWeatherDnsResolver::onFound, this);
        if(result == ERR_OK) {
            const uint32_t address = cached.u_addr.ip4.addr;
            pending_.store(false, std::memory_order_release);
            output = address;
            return address != 0;
        }
        if(result != ERR_INPROGRESS) {
            pending_.store(false, std::memory_order_release);
            return false;
        }
        while(weatherDeadlineOpen(clock, deadline_ms)) {
            if(completed_.load(std::memory_order_acquire)) {
                const uint32_t address =
                    address_.load(std::memory_order_relaxed);
                output = address;
                return address != 0;
            }
            clock.idle();
        }
        // The callback argument is this static resolver, never stack memory.
        // A timed-out lwIP query remains pending until its eventual callback;
        // retries fail closed meanwhile instead of reusing a stale callback.
        return false;
    }

private:
    static void onFound(const char *, const ip_addr_t * address,
                        void * context) {
        EspWeatherDnsResolver * resolver =
            static_cast<EspWeatherDnsResolver *>(context);
        resolver->address_.store(
            address ? address->u_addr.ip4.addr : 0,
            std::memory_order_relaxed);
        resolver->completed_.store(true, std::memory_order_release);
        resolver->pending_.store(false, std::memory_order_release);
    }

    std::atomic<bool> pending_{false};
    std::atomic<bool> completed_{false};
    std::atomic<uint32_t> address_{0};
};

EspWeatherDnsResolver weather_dns;

class EspWeatherHttpClient : public WeatherHttpClient {
public:
    bool get(const char * url, char * output, size_t capacity,
             size_t & output_length, int & status_code) override {
        output_length = 0;
        status_code = 0;
        if(!url || !output || capacity < 2) return false;
        constexpr char host[] = "api.open-meteo.com";
        constexpr char prefix[] = "https://api.open-meteo.com";
        const size_t prefix_length = sizeof(prefix) - 1;
        if(strncmp(url, prefix, prefix_length) != 0 ||
           url[prefix_length] != '/') return false;
        const char * path = url + prefix_length;
        const size_t path_length = strlen(path);
        if(path_length == 0 || path_length > 400 ||
           strchr(path, '\r') || strchr(path, '\n') || strchr(path, ' ')) {
            return false;
        }
        const uint32_t started_ms = millis();
        const uint32_t absolute_deadline_ms =
            started_ms + WeatherService::kRequestTimeoutMs;
        EspWeatherDeadlineClock clock;
        IPAddress address;
        const uint32_t dns_deadline_ms = started_ms + 5000UL;
        if(!weather_dns.resolve(host, clock, dns_deadline_ms, address) ||
           !weatherDeadlineOpen(clock, absolute_deadline_ms)) return false;
        WiFiClientSecure secure;
        uint32_t remaining_ms = remainingWeatherBudget(started_ms);
        // ESP32 Arduino 2.0.17 copies the socket timeout into the TLS
        // context during connect().  A later setTimeout() call does not
        // shorten send_ssl_data(), so TCP, TLS and the request write must
        // share one pre-connect budget.
        constexpr uint32_t kResponseAndOverheadReserveMs = 3000UL;
        constexpr uint32_t kBlockingStageCount = 3UL;
        constexpr uint32_t kWriteOverrunGuardMs = 250UL;
        if(remaining_ms <= kResponseAndOverheadReserveMs) return false;
        const uint32_t stage_budget_ms =
            (remaining_ms - kResponseAndOverheadReserveMs) /
            kBlockingStageCount;
        const uint32_t bounded_stage_ms = stage_budget_ms < 4000UL
            ? stage_budget_ms : 4000UL;
        const uint32_t stage_timeout_seconds = bounded_stage_ms / 1000UL;
        if(stage_timeout_seconds == 0) return false;
        const uint32_t stage_timeout_ms = stage_timeout_seconds * 1000UL;
        secure.setTimeout(stage_timeout_seconds);
        secure.setHandshakeTimeout(stage_timeout_seconds);
        if(!secure.connect(address, 443, host, kIsrgRootX1, nullptr, nullptr) ||
           remainingWeatherBudget(started_ms) == 0) {
            secure.stop();
            return false;
        }
        // send_ssl_data() still uses the timeout captured above.  Do not enter
        // it unless the original absolute deadline can cover that full wait.
        const uint32_t write_remaining_ms = remainingWeatherBudget(started_ms);
        if(write_remaining_ms <= stage_timeout_ms + kWriteOverrunGuardMs) {
            secure.stop();
            return false;
        }
        char request[512]{};
        const int request_length = snprintf(
            request, sizeof(request),
            "GET %s HTTP/1.1\r\nHost: %s\r\nAccept: application/json\r\n"
            "Accept-Encoding: identity\r\nConnection: close\r\n\r\n",
            path, host);
        if(request_length <= 0 ||
           static_cast<size_t>(request_length) >= sizeof(request) ||
           secure.write(reinterpret_cast<const uint8_t *>(request),
                        static_cast<size_t>(request_length)) !=
               static_cast<size_t>(request_length) ||
           remainingWeatherBudget(started_ms) == 0) {
            secure.stop();
            return false;
        }
        EspWeatherResponseStream stream(secure);
        const bool ok = WeatherHttpResponseReader::read(
            stream, clock, absolute_deadline_ms,
            output, capacity, output_length, status_code);
        secure.stop();
        return ok && status_code == 200 && output_length > 0;
    }
};

EspWeatherHttpClient default_http;

class WeatherRecursiveLock {
public:
    explicit WeatherRecursiveLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        locked_ = !mutex_ ||
            xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) == pdTRUE;
    }
    ~WeatherRecursiveLock() {
        if(locked_ && mutex_) xSemaphoreGiveRecursive(mutex_);
    }
private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
};

}  // namespace

bool WeatherHttpResponseReader::read(WeatherResponseStream & stream,
                                     WeatherDeadlineClock & clock,
                                     uint32_t deadline_ms,
                                     char * output,
                                     size_t capacity,
                                     size_t & output_length,
                                     int & status_code) {
    constexpr size_t kMaxHeaderBytes = 2048;
    output_length = 0;
    status_code = 0;
    if(!output || capacity < 2 ||
       !weatherDeadlineOpen(clock, deadline_ms)) return false;
    output[0] = '\0';

    char line[512]{};
    size_t line_length = 0;
    size_t header_bytes = 0;
    if(!readWeatherLine(stream, clock, deadline_ms, line, sizeof(line),
                        line_length, header_bytes) ||
       header_bytes > kMaxHeaderBytes ||
       sscanf(line, "HTTP/%*u.%*u %d", &status_code) != 1 ||
       status_code < 100 || status_code > 599) return false;

    bool has_content_length = false;
    bool chunked = false;
    size_t content_length = 0;
    while(true) {
        if(!readWeatherLine(stream, clock, deadline_ms, line, sizeof(line),
                            line_length, header_bytes) ||
           header_bytes > kMaxHeaderBytes) return false;
        if(line_length == 0) break;
        constexpr char content_length_header[] = "Content-Length:";
        constexpr char transfer_encoding_header[] = "Transfer-Encoding:";
        if(startsWithIgnoreCase(line, content_length_header)) {
            size_t parsed = 0;
            if(has_content_length ||
               !parseWeatherLength(line + sizeof(content_length_header) - 1,
                                   parsed)) return false;
            has_content_length = true;
            content_length = parsed;
        } else if(startsWithIgnoreCase(line, transfer_encoding_header)) {
            chunked = containsIgnoreCase(
                line + sizeof(transfer_encoding_header) - 1, "chunked");
        }
    }
    if(status_code != 200) return false;
    if(has_content_length && content_length > capacity - 1) {
        output_length = capacity;
        return false;
    }

    if(chunked) {
        size_t framing_bytes = 0;
        while(true) {
            if(!readWeatherLine(stream, clock, deadline_ms, line,
                                sizeof(line), line_length, framing_bytes) ||
               framing_bytes > kMaxHeaderBytes) return false;
            size_t chunk_length = 0;
            if(!parseChunkLength(line, chunk_length)) return false;
            if(chunk_length == 0) {
                do {
                    if(!readWeatherLine(stream, clock, deadline_ms, line,
                                        sizeof(line), line_length,
                                        framing_bytes) ||
                       framing_bytes > kMaxHeaderBytes) return false;
                } while(line_length != 0);
                return true;
            }
            if(chunk_length > capacity - 1 - output_length) {
                output_length = capacity;
                return false;
            }
            for(size_t index = 0; index < chunk_length; ++index) {
                char value = '\0';
                if(!readWeatherByte(stream, clock, deadline_ms, value) ||
                   !appendWeatherByte(value, output, capacity,
                                      output_length)) return false;
            }
            char carriage_return = '\0';
            char line_feed = '\0';
            if(!readWeatherByte(stream, clock, deadline_ms,
                                carriage_return) ||
               !readWeatherByte(stream, clock, deadline_ms, line_feed) ||
               carriage_return != '\r' || line_feed != '\n') return false;
        }
    }

    if(has_content_length) {
        for(size_t index = 0; index < content_length; ++index) {
            char value = '\0';
            if(!readWeatherByte(stream, clock, deadline_ms, value) ||
               !appendWeatherByte(value, output, capacity,
                                  output_length)) return false;
        }
        return true;
    }

    while(weatherDeadlineOpen(clock, deadline_ms)) {
        if(stream.available() > 0) {
            const int value = stream.read();
            if(value >= 0 && !appendWeatherByte(static_cast<char>(value),
                                                output, capacity,
                                                output_length)) return false;
        } else if(!stream.connected()) {
            return true;
        } else {
            clock.idle();
        }
    }
    return false;
}

bool LittleFsWeatherCacheStore::load(WeatherSnapshot & output,
                                     WeatherSource & source) {
    output = {};
    source = WeatherSource::None;
    WeatherCacheRecord record{};
    if(!loadWeatherRecord(kCachePath, record) &&
       !loadWeatherRecord(kCacheBackupPath, record)) return false;
    output = record.snapshot;
    source = static_cast<WeatherSource>(record.source);
    return true;
}

bool LittleFsWeatherCacheStore::save(const WeatherSnapshot & snapshot,
                                     WeatherSource source) {
    if(!snapshot.valid || source == WeatherSource::None) return false;
    WeatherCacheRecord record{};
    record.source = static_cast<uint8_t>(source);
    record.snapshot = snapshot;
    record.checksum = weatherRecordChecksum(record);
    File file = LittleFS.open(kCachePartPath, FILE_WRITE);
    if(!file) return false;
    const bool written = file.write(reinterpret_cast<const uint8_t *>(&record),
                                    sizeof(record)) == sizeof(record);
    file.flush();
    file.close();
    if(!written) {
        LittleFS.remove(kCachePartPath);
        return false;
    }
    WeatherCacheRecord previous{};
    const bool had_previous = loadWeatherRecord(kCachePath, previous);
    if(had_previous) {
        LittleFS.remove(kCacheBackupPath);
    } else if(LittleFS.exists(kCachePath)) {
        LittleFS.remove(kCachePath);
    }
    if(had_previous && !LittleFS.rename(kCachePath, kCacheBackupPath)) {
        LittleFS.remove(kCachePartPath);
        return false;
    }
    if(!LittleFS.rename(kCachePartPath, kCachePath)) {
        LittleFS.remove(kCachePartPath);
        if(had_previous) LittleFS.rename(kCacheBackupPath, kCachePath);
        return false;
    }
    WeatherCacheRecord verified{};
    if(!loadWeatherRecord(kCachePath, verified)) {
        LittleFS.remove(kCachePath);
        if(had_previous) LittleFS.rename(kCacheBackupPath, kCachePath);
        return false;
    }
    LittleFS.remove(kCacheBackupPath);
    return true;
}

bool LittleFsWeatherCacheStore::loadLocation(double & latitude,
                                             double & longitude,
                                             char city[32]) {
    File file = LittleFS.open(kLocationPath, FILE_READ);
    if(!file || file.size() != sizeof(WeatherLocationRecord)) return false;
    WeatherLocationRecord record{};
    const bool read = file.read(reinterpret_cast<uint8_t *>(&record),
                                sizeof(record)) == sizeof(record);
    file.close();
    if(!read || record.magic != 0x31434F4CUL || record.version != 1 ||
       !isfinite(record.latitude) || !isfinite(record.longitude) ||
       record.latitude < -90.0 || record.latitude > 90.0 ||
       record.longitude < -180.0 || record.longitude > 180.0 ||
       record.city[0] == '\0') return false;
    latitude = record.latitude;
    longitude = record.longitude;
    memcpy(city, record.city, sizeof(record.city));
    city[31] = '\0';
    return true;
}

bool LittleFsWeatherCacheStore::saveLocation(double latitude,
                                             double longitude,
                                             const char * city) {
    if(!city || !isfinite(latitude) || !isfinite(longitude)) return false;
    WeatherLocationRecord record{};
    record.latitude = latitude;
    record.longitude = longitude;
    strlcpy(record.city, city, sizeof(record.city));
    File file = LittleFS.open(kLocationPartPath, FILE_WRITE);
    if(!file) return false;
    const bool written = file.write(reinterpret_cast<const uint8_t *>(&record),
                                    sizeof(record)) == sizeof(record);
    file.flush();
    file.close();
    if(!written) {
        LittleFS.remove(kLocationPartPath);
        return false;
    }
    LittleFS.remove(kLocationPath);
    if(!LittleFS.rename(kLocationPartPath, kLocationPath)) {
        LittleFS.remove(kLocationPartPath);
        return false;
    }
    return true;
}

void LittleFsWeatherCacheStore::clearLocation() {
    LittleFS.remove(kLocationPartPath);
    LittleFS.remove(kLocationPath);
}

bool LittleFsWeatherCacheStore::clearSensitiveState() {
    bool ok = true;
    static const char * const paths[] = {
        kCachePartPath, kCacheBackupPath, kCachePath,
        kLocationPartPath, kLocationPath,
    };
    for(const char * path : paths) {
        if(LittleFS.exists(path)) ok = LittleFS.remove(path) && ok;
    }
    return ok;
}

WeatherService::WeatherService(WeatherCacheStore & cache)
    : cache_(cache) {
    mutex_ = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage_);
}

WeatherService::WeatherService(WeatherCacheStore & cache,
                               WifiService & wifi)
    : cache_(cache), wifi_(&wifi), http_(&default_http) {
    mutex_ = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage_);
}

WeatherService::WeatherService(WeatherCacheStore & cache,
                               WifiService & wifi,
                               WeatherHttpClient & http)
    : cache_(cache), wifi_(&wifi), http_(&http) {
    mutex_ = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage_);
}

void WeatherService::attachNetwork(WifiService & wifi,
                                   WeatherHttpClient & http) {
    WeatherRecursiveLock lock(mutex_);
    wifi_ = &wifi;
    http_ = &http;
}

bool WeatherService::loadCache() {
    WeatherRecursiveLock lock(mutex_);
    double latitude = 0.0;
    double longitude = 0.0;
    char city[32]{};
    if(cache_.loadLocation(latitude, longitude, city)) {
        latitude_ = latitude;
        longitude_ = longitude;
        strlcpy(location_city_, city, sizeof(location_city_));
        location_valid_ = true;
    }
    WeatherSnapshot loaded{};
    WeatherSource source = WeatherSource::None;
    if(!cache_.load(loaded, source) || !loaded.valid ||
       source == WeatherSource::None) return false;
    current_ = loaded;
    source_ = source;
    state_ = WeatherServiceState::Ready;
    return true;
}

bool WeatherService::configureLocation(double latitude,
                                       double longitude,
                                       const char * city) {
    WeatherRecursiveLock lock(mutex_);
    if(!isfinite(latitude) || !isfinite(longitude) ||
       latitude < -90.0 || latitude > 90.0 ||
       longitude < -180.0 || longitude > 180.0 || !city) return false;
    const size_t city_length = strlen(city);
    if(city_length == 0 || city_length >= sizeof(location_city_)) return false;
    if(!cache_.saveLocation(latitude, longitude, city)) return false;
    latitude_ = latitude;
    longitude_ = longitude;
    strlcpy(location_city_, city, sizeof(location_city_));
    location_valid_ = true;
    return true;
}

void WeatherService::clearLocation() {
    WeatherRecursiveLock lock(mutex_);
    latitude_ = 0.0;
    longitude_ = 0.0;
    location_city_[0] = '\0';
    location_valid_ = false;
    cache_.clearLocation();
}

bool WeatherService::clearSensitiveState() {
    WeatherRecursiveLock lock(mutex_);
    const bool cleared = cache_.clearSensitiveState();
    current_ = WeatherSnapshot{};
    source_ = WeatherSource::None;
    state_ = WeatherServiceState::Idle;
    latitude_ = 0.0;
    longitude_ = 0.0;
    location_city_[0] = '\0';
    location_valid_ = false;
    request_started_ms_ = 0;
    request_epoch_ = 0;
    response_[0] = '\0';
    return cleared;
}

bool WeatherService::validSnapshot(const WeatherSnapshot & snapshot,
                                   int64_t now_epoch) {
    if(!snapshot.valid || snapshot.city[0] == '\0' ||
       snapshot.updated_epoch <= 0 || now_epoch <= 0 ||
       snapshot.updated_epoch > now_epoch + 300 ||
       snapshot.updated_epoch < now_epoch - 7 * 24 * 60 * 60) return false;
    if(snapshot.temperature_tenths_c < -1000 ||
       snapshot.temperature_tenths_c > 700 ||
       snapshot.high_tenths_c < -1000 || snapshot.high_tenths_c > 700 ||
       snapshot.low_tenths_c < -1000 || snapshot.low_tenths_c > 700 ||
       snapshot.high_tenths_c < snapshot.low_tenths_c) return false;
    return true;
}

bool WeatherService::decodePhonePayload(const uint8_t * payload,
                                        size_t length,
                                        int64_t now_epoch,
                                        WeatherSnapshot & output) {
    output = {};
    if(!payload || length < 19 || (payload[0] != 1 && payload[0] != 2)) {
        return false;
    }
    const size_t fixed_bytes = payload[0] == 2 ? 26 : 18;
    const uint8_t city_length = payload[1];
    if(city_length == 0 || city_length >= sizeof(output.city) ||
       length != fixed_bytes + city_length) return false;
    output.weather_code = readU16(payload + 2);
    output.temperature_tenths_c = readI16(payload + 4);
    output.high_tenths_c = readI16(payload + 6);
    output.low_tenths_c = readI16(payload + 8);
    output.updated_epoch = readI64(payload + 10);
    output.valid = copyBoundedText(output.city, sizeof(output.city),
                                  payload + fixed_bytes, city_length);
    output.stale = false;
    if(!validSnapshot(output, now_epoch)) {
        output = {};
        return false;
    }
    return true;
}

bool WeatherService::commit(const WeatherSnapshot & snapshot,
                            WeatherSource source) {
    if(!cache_.save(snapshot, source)) return false;
    current_ = snapshot;
    source_ = source;
    state_ = WeatherServiceState::Ready;
    return true;
}

bool WeatherService::applyPhonePayload(const uint8_t * payload,
                                       size_t length,
                                       int64_t now_epoch) {
    WeatherRecursiveLock lock(mutex_);
    WeatherSnapshot decoded{};
    if(!decodePhonePayload(payload, length, now_epoch, decoded)) return false;
    if(payload[0] == 2) {
        const double latitude = readI32(payload + 18) / 1000000.0;
        const double longitude = readI32(payload + 22) / 1000000.0;
        if(!configureLocation(latitude, longitude, decoded.city)) return false;
    }
    return commit(decoded, WeatherSource::Phone);
}

bool WeatherService::applyPhoneSnapshot(const WeatherSnapshot & snapshot,
                                        int64_t now_epoch) {
    WeatherRecursiveLock lock(mutex_);
    return validSnapshot(snapshot, now_epoch) &&
        commit(snapshot, WeatherSource::Phone);
}

bool WeatherService::applyDirectSnapshot(const WeatherSnapshot & snapshot,
                                         int64_t now_epoch) {
    WeatherRecursiveLock lock(mutex_);
    if(!validSnapshot(snapshot, now_epoch)) return false;
    if(source_ == WeatherSource::Phone &&
       freshness(now_epoch) == WeatherFreshness::Fresh) return false;
    return commit(snapshot, WeatherSource::Direct);
}

WeatherFreshness WeatherService::freshness(int64_t now_epoch) const {
    WeatherRecursiveLock lock(mutex_);
    if(!current_.valid || now_epoch <= 0) return WeatherFreshness::Expired;
    const int64_t age = now_epoch > current_.updated_epoch
        ? now_epoch - current_.updated_epoch : 0;
    if(age > kOldAfterSeconds) return WeatherFreshness::Old;
    if(age > kStaleAfterSeconds) return WeatherFreshness::Stale;
    return WeatherFreshness::Fresh;
}

WeatherSnapshot WeatherService::snapshot(int64_t now_epoch) const {
    WeatherRecursiveLock lock(mutex_);
    WeatherSnapshot result = current_;
    result.stale = freshness(now_epoch) != WeatherFreshness::Fresh;
    return result;
}

bool WeatherService::parseOpenMeteoJson(const char * json,
                                        size_t length,
                                        const char * city,
                                        int64_t updated_epoch,
                                        WeatherSnapshot & output) {
    output = {};
    if(!json || !city || length == 0 || length > kMaxResponseBytes ||
       strnlen(city, sizeof(output.city)) >= sizeof(output.city)) return false;
    double current = 0.0;
    double code = 0.0;
    double high = 0.0;
    double low = 0.0;
    if(!parseNumberInSection(json, length, "\"current\"",
                             "\"temperature_2m\":", current) ||
       !parseNumberInSection(json, length, "\"current\"",
                             "\"weather_code\":", code) ||
       !parseNumberInSection(json, length, "\"daily\"",
                             "\"temperature_2m_max\":", high) ||
       !parseNumberInSection(json, length, "\"daily\"",
                             "\"temperature_2m_min\":", low) ||
       code < 0 || code > 65535 || floor(code) != code) return false;
    bool current_ok = false;
    bool high_ok = false;
    bool low_ok = false;
    output.temperature_tenths_c = toTenths(current, current_ok);
    output.high_tenths_c = toTenths(high, high_ok);
    output.low_tenths_c = toTenths(low, low_ok);
    if(!current_ok || !high_ok || !low_ok ||
       output.high_tenths_c < output.low_tenths_c) return false;
    output.weather_code = static_cast<uint16_t>(code);
    output.updated_epoch = updated_epoch;
    strlcpy(output.city, city, sizeof(output.city));
    output.valid = updated_epoch > 0;
    output.stale = false;
    return output.valid;
}

bool WeatherService::requestRefresh(uint32_t now_ms, int64_t now_epoch) {
    WeatherRecursiveLock lock(mutex_);
    if(state_ == WeatherServiceState::WaitingForWifi ||
       state_ == WeatherServiceState::Updating) return true;
    if(source_ == WeatherSource::Phone &&
       freshness(now_epoch) == WeatherFreshness::Fresh) {
        state_ = WeatherServiceState::Ready;
        return true;
    }
    if(!location_valid_) {
        state_ = WeatherServiceState::NoLocation;
        return false;
    }
    if(!wifi_ || !http_ || !wifi_->request(WifiPurpose::Weather, now_ms)) {
        state_ = WeatherServiceState::NoNetwork;
        return false;
    }
    request_started_ms_ = now_ms;
    request_epoch_ = now_epoch;
    state_ = WeatherServiceState::WaitingForWifi;
    return true;
}

void WeatherService::finishNetworkRequest() {
    if(wifi_) wifi_->release(WifiPurpose::Weather, millis());
}

void WeatherService::tick(uint32_t now_ms, int64_t now_epoch) {
    double latitude = 0.0;
    double longitude = 0.0;
    char city[32]{};
    WeatherHttpClient * http = nullptr;
    int64_t effective_epoch = 0;
    {
        WeatherRecursiveLock lock(mutex_);
        if(state_ != WeatherServiceState::WaitingForWifi) return;
        if(now_ms - request_started_ms_ > kRequestTimeoutMs) {
            state_ = WeatherServiceState::NoNetwork;
            finishNetworkRequest();
            return;
        }
        if(!wifi_ || wifi_->mode() != WifiMode::Connected) return;
        state_ = WeatherServiceState::Updating;
        latitude = latitude_;
        longitude = longitude_;
        strlcpy(city, location_city_, sizeof(city));
        http = http_;
        effective_epoch = now_epoch > 0 ? now_epoch : request_epoch_;
    }
    char url[384]{};
    const int url_length = snprintf(
        url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=2",
        latitude, longitude);
    size_t response_length = 0;
    int status_code = 0;
    const bool received = url_length > 0 &&
        static_cast<size_t>(url_length) < sizeof(url) &&
        http && http->get(url, response_, sizeof(response_), response_length,
                          status_code);
    WeatherRecursiveLock lock(mutex_);
    if(response_length > kMaxResponseBytes) {
        state_ = WeatherServiceState::ResponseTooLarge;
        finishNetworkRequest();
        return;
    }
    WeatherSnapshot parsed{};
    if(!received || status_code != 200 ||
       !parseOpenMeteoJson(response_, response_length, city,
                          effective_epoch, parsed) ||
       !applyDirectSnapshot(parsed, effective_epoch)) {
        if(source_ == WeatherSource::Phone &&
           freshness(effective_epoch) == WeatherFreshness::Fresh) {
            state_ = WeatherServiceState::Ready;
        } else {
            state_ = WeatherServiceState::ServiceError;
        }
    }
    finishNetworkRequest();
}

bool WeatherService::hasLocation() const {
    WeatherRecursiveLock lock(mutex_);
    return location_valid_;
}

WeatherSource WeatherService::source() const {
    WeatherRecursiveLock lock(mutex_);
    return source_;
}

WeatherServiceState WeatherService::state() const {
    WeatherRecursiveLock lock(mutex_);
    return state_;
}

}  // namespace firefly
