#pragma once

#include <FS.h>
#include <stddef.h>
#include <stdint.h>

#include "../hal/Es8311Device.h"

namespace firefly {

class StorageService;

enum class AudioUse : uint8_t {
    None,
    System,
    Music,
    Recorder,
    Alarm,
};

class AudioSessionArbiter {
public:
    bool acquire(AudioUse requested);
    void release(AudioUse owner);
    AudioUse current() const { return current_; }

private:
    static uint8_t priority(AudioUse use);
    AudioUse current_ = AudioUse::None;
};

struct WavInfo {
    uint32_t sample_rate = 0;
    uint32_t data_offset = 0;
    uint32_t data_bytes = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
};

class AudioService {
public:
    using StartAllowedCallback = bool (*)(AudioUse use, void * context);
    static constexpr int kMclkPin = 41;
    static constexpr int kBclkPin = 45;
    static constexpr int kWordSelectPin = 40;
    static constexpr int kDataOutPin = 42;
    static constexpr int kDataInPin = 16;
    static constexpr int kAmplifierPin = 46;
    static constexpr size_t kWavHeaderBytes = 44;

    explicit AudioService(Es8311Device & codec) : codec_(codec) {}
    void setStartAllowedCallback(StartAllowedCallback callback,
                                 void * context = nullptr) {
        start_allowed_callback_ = callback;
        start_allowed_context_ = context;
    }

    bool begin();
    bool playPcm(const int16_t * samples,
                 size_t frames,
                 uint32_t sample_rate,
                 AudioUse use);
    bool startLoopingPcm(const int16_t * samples,
                         size_t frames,
                         uint32_t sample_rate,
                         AudioUse use);
    void service();
    bool playWav(StorageService & storage, const char * path, AudioUse use);
    bool startWav(StorageService & storage, const char * path, AudioUse use);
    bool pausePlayback();
    bool resumePlayback();
    void discardPlayback();
    bool playbackPaused() const { return playback_paused_; }
    uint32_t playbackPositionMs() const;
    uint32_t playbackDurationMs() const;
    bool startRecording(StorageService & storage,
                        const char * temporary_path,
                        uint32_t sample_rate = 16000);
    size_t serviceRecording(size_t max_bytes = 512);
    void stop();
    void setVolume(uint8_t percent);
    AudioUse activeUse() const { return arbiter_.current(); }
    bool ready() const { return i2s_installed_; }

    static bool parseWavHeader(const uint8_t * header,
                               size_t length,
                               WavInfo & info);
    static bool buildWavHeader(uint8_t * header,
                               size_t length,
                               uint32_t data_bytes,
                               uint32_t sample_rate,
                               uint16_t channels);
    static uint16_t cleanupTemporaryRecordings(
        StorageService & storage,
        const char * directory = "/FireflyOS/Recordings");

private:
    bool beginSession(AudioUse use, uint32_t sample_rate, bool playback);
    bool configureRate(uint32_t sample_rate);
    bool writeMono(const int16_t * samples, size_t frames);
    bool writeStereo(const int16_t * samples, size_t frames);
    bool finishRecording();
    void closeHardwareSession(AudioUse owner);
    static bool safeRecordingPath(const char * path);

    Es8311Device & codec_;
    AudioSessionArbiter arbiter_{};
    fs::File recording_file_{};
    StorageService * recording_storage_ = nullptr;
    uint32_t recording_data_bytes_ = 0;
    uint32_t recording_sample_rate_ = 0;
    const int16_t * looping_samples_ = nullptr;
    size_t looping_frames_ = 0;
    size_t looping_cursor_ = 0;
    fs::File playback_file_{};
    StorageService * playback_storage_ = nullptr;
    AudioUse playback_use_ = AudioUse::None;
    uint32_t playback_data_bytes_ = 0;
    uint32_t playback_remaining_bytes_ = 0;
    uint32_t playback_sample_rate_ = 0;
    uint16_t playback_channels_ = 0;
    bool playback_paused_ = false;
    uint8_t volume_ = 50;
    bool i2s_installed_ = false;
    StartAllowedCallback start_allowed_callback_ = nullptr;
    void * start_allowed_context_ = nullptr;
};

}  // namespace firefly
