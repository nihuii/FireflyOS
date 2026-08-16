#pragma once

#include <stddef.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "CompanionSyncService.h"
#include "WifiService.h"

namespace firefly {

struct WeatherSnapshot {
    int16_t temperature_tenths_c = 0;
    int16_t high_tenths_c = 0;
    int16_t low_tenths_c = 0;
    uint16_t weather_code = 0;
    int64_t updated_epoch = 0;
    char city[32]{};
    bool valid = false;
    bool stale = true;
};

enum class WeatherSource : uint8_t {
    None,
    Phone,
    Direct
};

enum class WeatherServiceState : uint8_t {
    Idle,
    WaitingForWifi,
    Updating,
    Ready,
    NoLocation,
    NoNetwork,
    ResponseTooLarge,
    ServiceError
};

class WeatherCacheStore {
public:
    virtual ~WeatherCacheStore() = default;
    virtual bool load(WeatherSnapshot & output, WeatherSource & source) = 0;
    virtual bool save(const WeatherSnapshot & snapshot,
                      WeatherSource source) = 0;
    virtual bool loadLocation(double & latitude,
                              double & longitude,
                              char city[32]) {
        (void)latitude; (void)longitude; (void)city;
        return false;
    }
    virtual bool saveLocation(double latitude,
                              double longitude,
                              const char * city) {
        (void)latitude; (void)longitude; (void)city;
        return false;
    }
    virtual void clearLocation() {}
    virtual bool clearSensitiveState() {
        clearLocation();
        return true;
    }
};

class WeatherHttpClient {
public:
    virtual ~WeatherHttpClient() = default;
    virtual bool get(const char * url,
                     char * output,
                     size_t capacity,
                     size_t & output_length,
                     int & status_code) = 0;
};

class WeatherResponseStream {
public:
    virtual ~WeatherResponseStream() = default;
    virtual bool connected() = 0;
    virtual int available() = 0;
    virtual int read() = 0;
};

class WeatherDeadlineClock {
public:
    virtual ~WeatherDeadlineClock() = default;
    virtual uint32_t nowMs() const = 0;
    virtual void idle() = 0;
};

class WeatherHttpResponseReader {
public:
    static bool read(WeatherResponseStream & stream,
                     WeatherDeadlineClock & clock,
                     uint32_t deadline_ms,
                     char * output,
                     size_t capacity,
                     size_t & output_length,
                     int & status_code);
};

class LittleFsWeatherCacheStore : public WeatherCacheStore {
public:
    bool load(WeatherSnapshot & output, WeatherSource & source) override;
    bool save(const WeatherSnapshot & snapshot,
              WeatherSource source) override;
    bool loadLocation(double & latitude,
                      double & longitude,
                      char city[32]) override;
    bool saveLocation(double latitude,
                      double longitude,
                      const char * city) override;
    void clearLocation() override;
    bool clearSensitiveState() override;
};

class WeatherService {
public:
    static constexpr size_t kMaxResponseBytes = 8192;
    static constexpr int64_t kStaleAfterSeconds = 3 * 60 * 60;
    static constexpr int64_t kOldAfterSeconds = 24 * 60 * 60;
    static constexpr uint32_t kRequestTimeoutMs = 15000;

    explicit WeatherService(WeatherCacheStore & cache);
    WeatherService(WeatherCacheStore & cache, WifiService & wifi);
    WeatherService(WeatherCacheStore & cache,
                   WifiService & wifi,
                   WeatherHttpClient & http);

    void attachNetwork(WifiService & wifi, WeatherHttpClient & http);
    bool loadCache();
    bool configureLocation(double latitude,
                           double longitude,
                           const char * city);
    void clearLocation();
    bool clearSensitiveState();
    bool hasLocation() const;
    bool applyPhonePayload(const uint8_t * payload,
                           size_t length,
                           int64_t now_epoch);
    bool applyPhoneSnapshot(const WeatherSnapshot & snapshot,
                            int64_t now_epoch);
    bool applyDirectSnapshot(const WeatherSnapshot & snapshot,
                             int64_t now_epoch);
    bool requestRefresh(uint32_t now_ms, int64_t now_epoch);
    void tick(uint32_t now_ms, int64_t now_epoch);
    WeatherSnapshot snapshot(int64_t now_epoch) const;
    WeatherFreshness freshness(int64_t now_epoch) const;
    WeatherSource source() const;
    WeatherServiceState state() const;

    static bool decodePhonePayload(const uint8_t * payload,
                                   size_t length,
                                   int64_t now_epoch,
                                   WeatherSnapshot & output);
    static bool parseOpenMeteoJson(const char * json,
                                   size_t length,
                                   const char * city,
                                   int64_t updated_epoch,
                                   WeatherSnapshot & output);

private:
    static bool validSnapshot(const WeatherSnapshot & snapshot,
                              int64_t now_epoch);
    bool commit(const WeatherSnapshot & snapshot, WeatherSource source);
    void finishNetworkRequest();

    WeatherCacheStore & cache_;
    WifiService * wifi_ = nullptr;
    WeatherHttpClient * http_ = nullptr;
    WeatherSnapshot current_{};
    WeatherSource source_ = WeatherSource::None;
    WeatherServiceState state_ = WeatherServiceState::Idle;
    double latitude_ = 0.0;
    double longitude_ = 0.0;
    char location_city_[32]{};
    bool location_valid_ = false;
    uint32_t request_started_ms_ = 0;
    int64_t request_epoch_ = 0;
    char response_[kMaxResponseBytes + 1]{};
    mutable StaticSemaphore_t mutex_storage_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
};

static_assert(sizeof(WeatherSnapshot) <= 56,
              "weather snapshot must remain fixed and bounded");

}  // namespace firefly
