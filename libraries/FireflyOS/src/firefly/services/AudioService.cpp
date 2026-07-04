#include "AudioService.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <limits.h>
#include <string.h>

#include "StorageService.h"

namespace firefly {
namespace {

constexpr i2s_port_t kI2sPort = I2S_NUM_0;

uint16_t readLe16(const uint8_t * bytes) {
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(bytes[1] << 8);
}

uint32_t readLe32(const uint8_t * bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

void writeLe16(uint8_t * bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
}

void writeLe32(uint8_t * bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
    bytes[2] = static_cast<uint8_t>(value >> 16);
    bytes[3] = static_cast<uint8_t>(value >> 24);
}

bool endsWith(const char * value, const char * suffix) {
    if(!value || !suffix) return false;
    const size_t value_length = strlen(value);
    const size_t suffix_length = strlen(suffix);
    return value_length >= suffix_length &&
           strcmp(value + value_length - suffix_length, suffix) == 0;
}

}  // namespace

bool AudioSessionArbiter::acquire(AudioUse requested) {
    if(requested == AudioUse::None) return false;
    if(current_ == AudioUse::None || current_ == requested ||
       priority(requested) > priority(current_)) {
        current_ = requested;
        return true;
    }
    return false;
}

void AudioSessionArbiter::release(AudioUse owner) {
    if(current_ == owner) current_ = AudioUse::None;
}

uint8_t AudioSessionArbiter::priority(AudioUse use) {
    switch(use) {
        case AudioUse::Alarm: return 4;
        case AudioUse::Recorder: return 3;
        case AudioUse::System: return 2;
        case AudioUse::Music: return 1;
        default: return 0;
    }
}

bool AudioService::begin() {
    pinMode(kAmplifierPin, OUTPUT);
    digitalWrite(kAmplifierPin, LOW);
    if(i2s_installed_) return true;

    i2s_config_t config{};
    config.mode = static_cast<i2s_mode_t>(
        I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
    config.sample_rate = 16000;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 4;
    config.dma_buf_len = 256;
    config.use_apll = false;
    config.tx_desc_auto_clear = true;
    config.fixed_mclk = 0;
    config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

    if(i2s_driver_install(kI2sPort, &config, 0, nullptr) != ESP_OK) return false;
    i2s_pin_config_t pins{};
    pins.mck_io_num = kMclkPin;
    pins.bck_io_num = kBclkPin;
    pins.ws_io_num = kWordSelectPin;
    pins.data_out_num = kDataOutPin;
    pins.data_in_num = kDataInPin;
    if(i2s_set_pin(kI2sPort, &pins) != ESP_OK ||
       i2s_zero_dma_buffer(kI2sPort) != ESP_OK || !codec_.begin(16000)) {
        i2s_driver_uninstall(kI2sPort);
        return false;
    }
    codec_.setVolume(volume_);
    codec_.mute(true);
    i2s_installed_ = true;
    return true;
}

bool AudioService::playPcm(const int16_t * samples,
                           size_t frames,
                           uint32_t sample_rate,
                           AudioUse use) {
    if(!samples || frames == 0 || !beginSession(use, sample_rate, true)) {
        return false;
    }
    const bool ok = writeMono(samples, frames);
    stop();
    return ok;
}

bool AudioService::startLoopingPcm(const int16_t * samples,
                                   size_t frames,
                                   uint32_t sample_rate,
                                   AudioUse use) {
    if(!samples || frames == 0 || use == AudioUse::None ||
       !beginSession(use, sample_rate, true)) {
        return false;
    }
    looping_samples_ = samples;
    looping_frames_ = frames;
    looping_cursor_ = 0;
    return true;
}

void AudioService::service() {
    if(looping_samples_ && looping_frames_ > 0 &&
       arbiter_.current() != AudioUse::None) {
        int16_t mono[128];
        for(size_t i = 0; i < sizeof(mono) / sizeof(mono[0]); ++i) {
            mono[i] = looping_samples_[looping_cursor_];
            looping_cursor_ = (looping_cursor_ + 1) % looping_frames_;
        }
        if(!writeMono(mono, sizeof(mono) / sizeof(mono[0]))) stop();
        return;
    }

    if(!playback_file_ || playback_paused_ ||
       arbiter_.current() != playback_use_) return;
    if(playback_remaining_bytes_ == 0) {
        stop();
        return;
    }
    int16_t samples[256];
    size_t request = playback_remaining_bytes_ < sizeof(samples)
        ? playback_remaining_bytes_ : sizeof(samples);
    request &= ~static_cast<size_t>(1);
    const size_t received = playback_storage_->readManaged(playback_file_,
        reinterpret_cast<uint8_t *>(samples), request);
    const size_t sample_count = received / sizeof(int16_t);
    const bool written = received == request &&
        (playback_channels_ == 1
            ? writeMono(samples, sample_count)
            : writeStereo(samples, sample_count / 2));
    if(!written) {
        stop();
        return;
    }
    playback_remaining_bytes_ -= static_cast<uint32_t>(received);
    if(playback_remaining_bytes_ == 0) stop();
}

bool AudioService::startWav(StorageService & storage,
                            const char * path,
                            AudioUse use) {
    if(!path || use == AudioUse::None) return false;
    fs::File file = storage.openManaged(path, FILE_READ);
    if(!file) return false;
    bool is_directory = false;
    uint64_t file_size = 0;
    if(!storage.managedFileIsDirectory(file, is_directory) || is_directory ||
       !storage.managedFileSize(file, file_size)) {
        storage.closeManaged(file);
        return false;
    }
    uint8_t header[kWavHeaderBytes]{};
    WavInfo info{};
    if(storage.readManaged(file, header, sizeof(header)) != sizeof(header) ||
       !parseWavHeader(header, sizeof(header), info) ||
       file_size < info.data_offset + info.data_bytes ||
       !storage.seekManaged(file, info.data_offset) ||
       !beginSession(use, info.sample_rate, true)) {
        storage.closeManaged(file);
        return false;
    }
    if(playback_file_ && playback_storage_) playback_storage_->closeManaged(playback_file_);
    playback_file_ = file;
    playback_storage_ = &storage;
    playback_use_ = use;
    playback_data_bytes_ = info.data_bytes;
    playback_remaining_bytes_ = info.data_bytes;
    playback_sample_rate_ = info.sample_rate;
    playback_channels_ = info.channels;
    playback_paused_ = false;
    return true;
}

bool AudioService::pausePlayback() {
    if(!playback_file_ || playback_paused_ ||
       arbiter_.current() != playback_use_) return false;
    playback_paused_ = true;
    closeHardwareSession(playback_use_);
    return true;
}

bool AudioService::resumePlayback() {
    if(!playback_file_ || !playback_paused_ ||
       !beginSession(playback_use_, playback_sample_rate_, true)) return false;
    playback_paused_ = false;
    return true;
}

void AudioService::discardPlayback() {
    if(playback_use_ != AudioUse::None &&
       arbiter_.current() == playback_use_) {
        stop();
        return;
    }
    if(playback_file_ && playback_storage_) playback_storage_->closeManaged(playback_file_);
    playback_storage_ = nullptr;
    playback_use_ = AudioUse::None;
    playback_data_bytes_ = 0;
    playback_remaining_bytes_ = 0;
    playback_sample_rate_ = 0;
    playback_channels_ = 0;
    playback_paused_ = false;
}

uint32_t AudioService::playbackPositionMs() const {
    if(playback_data_bytes_ == 0 || playback_sample_rate_ == 0 ||
       playback_channels_ == 0) return 0;
    const uint32_t consumed = playback_data_bytes_ - playback_remaining_bytes_;
    const uint32_t bytes_per_second = playback_sample_rate_ *
        playback_channels_ * sizeof(int16_t);
    return bytes_per_second == 0
        ? 0 : static_cast<uint32_t>((static_cast<uint64_t>(consumed) * 1000ULL) /
                                    bytes_per_second);
}

uint32_t AudioService::playbackDurationMs() const {
    if(playback_data_bytes_ == 0 || playback_sample_rate_ == 0 ||
       playback_channels_ == 0) return 0;
    const uint32_t bytes_per_second = playback_sample_rate_ *
        playback_channels_ * sizeof(int16_t);
    return bytes_per_second == 0
        ? 0 : static_cast<uint32_t>((static_cast<uint64_t>(playback_data_bytes_) *
                                    1000ULL) / bytes_per_second);
}

bool AudioService::playWav(StorageService & storage,
                           const char * path,
                           AudioUse use) {
    if(!path) return false;
    fs::File file = storage.openManaged(path, FILE_READ);
    if(!file) return false;
    bool is_directory = false;
    uint64_t file_size = 0;
    if(!storage.managedFileIsDirectory(file, is_directory) || is_directory ||
       !storage.managedFileSize(file, file_size)) {
        storage.closeManaged(file);
        return false;
    }
    uint8_t header[kWavHeaderBytes]{};
    WavInfo info{};
    if(storage.readManaged(file, header, sizeof(header)) != sizeof(header) ||
       !parseWavHeader(header, sizeof(header), info) ||
       file_size < info.data_offset + info.data_bytes ||
       !storage.seekManaged(file, info.data_offset) ||
       !beginSession(use, info.sample_rate, true)) {
        storage.closeManaged(file);
        return false;
    }

    bool ok = true;
    uint32_t remaining = info.data_bytes;
    int16_t samples[256];
    while(remaining > 0 && arbiter_.current() == use) {
        size_t request = remaining < sizeof(samples) ? remaining : sizeof(samples);
        request &= ~static_cast<size_t>(1);
        const size_t received = storage.readManaged(file,
            reinterpret_cast<uint8_t *>(samples), request);
        if(received != request) {
            ok = false;
            break;
        }
        const size_t sample_count = received / sizeof(int16_t);
        ok = info.channels == 1
            ? writeMono(samples, sample_count)
            : writeStereo(samples, sample_count / 2);
        if(!ok) break;
        remaining -= static_cast<uint32_t>(received);
    }
    storage.closeManaged(file);
    stop();
    return ok && remaining == 0;
}

bool AudioService::startRecording(StorageService & storage,
                                  const char * temporary_path,
                                  uint32_t sample_rate) {
    if(!safeRecordingPath(temporary_path) ||
       !beginSession(AudioUse::Recorder, sample_rate, false)) {
        return false;
    }
    recording_file_ = storage.openManaged(temporary_path, FILE_WRITE);
    bool is_directory = false;
    if(!recording_file_ ||
       !storage.managedFileIsDirectory(recording_file_, is_directory) ||
       is_directory) {
        if(recording_file_) storage.closeManaged(recording_file_);
        stop();
        return false;
    }
    recording_storage_ = &storage;
    uint8_t header[kWavHeaderBytes]{};
    if(!buildWavHeader(header, sizeof(header), 0, sample_rate, 1) ||
       storage.writeManaged(recording_file_, header, sizeof(header)) != sizeof(header)) {
        storage.closeManaged(recording_file_);
        stop();
        return false;
    }
    recording_data_bytes_ = 0;
    recording_sample_rate_ = sample_rate;
    return true;
}

size_t AudioService::serviceRecording(size_t max_bytes) {
    if(arbiter_.current() != AudioUse::Recorder || !recording_file_) return 0;
    if(max_bytes < 4) return 0;
    if(max_bytes > 512) max_bytes = 512;
    max_bytes &= ~static_cast<size_t>(3);

    int16_t stereo[256]{};
    size_t bytes_read = 0;
    if(i2s_read(kI2sPort, stereo, max_bytes, &bytes_read,
                pdMS_TO_TICKS(25)) != ESP_OK || bytes_read < 4) {
        return 0;
    }
    const size_t stereo_samples = bytes_read / sizeof(int16_t);
    int16_t mono[128];
    const size_t frames = stereo_samples / 2;
    for(size_t i = 0; i < frames; ++i) mono[i] = stereo[i * 2];
    const size_t mono_bytes = frames * sizeof(int16_t);
    if(!recording_storage_ ||
       recording_storage_->writeManaged(recording_file_,
           reinterpret_cast<uint8_t *>(mono), mono_bytes) !=
       mono_bytes) {
        stop();
        return 0;
    }
    if(recording_data_bytes_ <= UINT32_MAX - mono_bytes) {
        recording_data_bytes_ += static_cast<uint32_t>(mono_bytes);
    } else {
        stop();
        return 0;
    }
    return mono_bytes;
}

void AudioService::stop() {
    const AudioUse owner = arbiter_.current();
    if(owner == AudioUse::None) {
        if(playback_file_ && playback_storage_) playback_storage_->closeManaged(playback_file_);
        playback_storage_ = nullptr;
        playback_use_ = AudioUse::None;
        playback_data_bytes_ = 0;
        playback_remaining_bytes_ = 0;
        playback_sample_rate_ = 0;
        playback_channels_ = 0;
        playback_paused_ = false;
        return;
    }
    looping_samples_ = nullptr;
    looping_frames_ = 0;
    looping_cursor_ = 0;
    if(playback_file_ && owner == playback_use_) {
        if(playback_storage_) playback_storage_->closeManaged(playback_file_);
        playback_storage_ = nullptr;
        playback_use_ = AudioUse::None;
        playback_data_bytes_ = 0;
        playback_remaining_bytes_ = 0;
        playback_sample_rate_ = 0;
        playback_channels_ = 0;
        playback_paused_ = false;
    }
    if(owner == AudioUse::Recorder) finishRecording();
    closeHardwareSession(owner);
}

void AudioService::setVolume(uint8_t percent) {
    volume_ = percent > 100 ? 100 : percent;
    if(codec_.initialized()) codec_.setVolume(volume_);
}

bool AudioService::parseWavHeader(const uint8_t * header,
                                  size_t length,
                                  WavInfo & info) {
    info = WavInfo{};
    if(!header || length < kWavHeaderBytes ||
       memcmp(header, "RIFF", 4) != 0 ||
       memcmp(header + 8, "WAVE", 4) != 0 ||
       memcmp(header + 12, "fmt ", 4) != 0 ||
       readLe32(header + 16) != 16 || readLe16(header + 20) != 1 ||
       memcmp(header + 36, "data", 4) != 0) {
        return false;
    }
    info.channels = readLe16(header + 22);
    info.sample_rate = readLe32(header + 24);
    info.bits_per_sample = readLe16(header + 34);
    info.data_offset = kWavHeaderBytes;
    info.data_bytes = readLe32(header + 40);
    const uint16_t block_align = readLe16(header + 32);
    const uint32_t byte_rate = readLe32(header + 28);
    return (info.channels == 1 || info.channels == 2) &&
           info.bits_per_sample == 16 && info.data_bytes > 0 &&
           Es8311Device::supportsSampleRate(info.sample_rate) &&
           block_align == info.channels * sizeof(int16_t) &&
           byte_rate == info.sample_rate * block_align;
}

bool AudioService::buildWavHeader(uint8_t * header,
                                  size_t length,
                                  uint32_t data_bytes,
                                  uint32_t sample_rate,
                                  uint16_t channels) {
    if(!header || length < kWavHeaderBytes ||
       (channels != 1 && channels != 2) ||
       !Es8311Device::supportsSampleRate(sample_rate) ||
       data_bytes > UINT32_MAX - 36) {
        return false;
    }
    memset(header, 0, kWavHeaderBytes);
    memcpy(header, "RIFF", 4);
    writeLe32(header + 4, data_bytes + 36);
    memcpy(header + 8, "WAVEfmt ", 8);
    writeLe32(header + 16, 16);
    writeLe16(header + 20, 1);
    writeLe16(header + 22, channels);
    writeLe32(header + 24, sample_rate);
    const uint16_t block_align = channels * sizeof(int16_t);
    writeLe32(header + 28, sample_rate * block_align);
    writeLe16(header + 32, block_align);
    writeLe16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    writeLe32(header + 40, data_bytes);
    return true;
}

uint16_t AudioService::cleanupTemporaryRecordings(StorageService & storage,
                                                  const char * directory_path) {
    if(!directory_path) return 0;
    fs::File directory = storage.openManaged(directory_path, FILE_READ);
    if(!directory) return 0;
    bool is_directory = false;
    if(!storage.managedFileIsDirectory(directory, is_directory) || !is_directory) {
        storage.closeManaged(directory);
        return 0;
    }
    uint16_t removed = 0;
    uint16_t scanned = 0;
    fs::File entry = storage.openNextManaged(directory);
    while(entry && scanned < 128) {
        ++scanned;
        bool entry_is_directory = false;
        char name[64]{};
        char path[160]{};
        const bool metadata_ok =
            storage.managedFileIsDirectory(entry, entry_is_directory) &&
            storage.managedFileName(entry, name, sizeof(name));
        const bool temporary = metadata_ok && !entry_is_directory &&
            endsWith(name, ".tmp") &&
            storage.managedFilePath(entry, path, sizeof(path));
        storage.closeManaged(entry);
        if(temporary && path[0] && storage.removeManaged(path)) ++removed;
        entry = storage.openNextManaged(directory);
    }
    if(entry) storage.closeManaged(entry);
    storage.closeManaged(directory);
    return removed;
}

bool AudioService::beginSession(AudioUse use,
                                uint32_t sample_rate,
                                bool playback) {
    if(!i2s_installed_ && !begin()) return false;
    const AudioUse previous = arbiter_.current();
    if(!arbiter_.acquire(use)) return false;
    if(previous != AudioUse::None && previous != use) {
        if(previous == AudioUse::Recorder) finishRecording();
        if(playback_file_ && previous == playback_use_) {
            playback_paused_ = true;
        }
        looping_samples_ = nullptr;
        looping_frames_ = 0;
        looping_cursor_ = 0;
        digitalWrite(kAmplifierPin, LOW);
        codec_.sleep();
    }
    if(!configureRate(sample_rate)) {
        arbiter_.release(use);
        return false;
    }
    i2s_zero_dma_buffer(kI2sPort);
    if(playback) {
        codec_.setVolume(volume_);
        codec_.mute(false);
        digitalWrite(kAmplifierPin, HIGH);
    } else {
        digitalWrite(kAmplifierPin, LOW);
        codec_.mute(true);
    }
    return true;
}

bool AudioService::configureRate(uint32_t sample_rate) {
    if(!Es8311Device::supportsSampleRate(sample_rate) ||
       i2s_set_clk(kI2sPort, sample_rate, I2S_BITS_PER_SAMPLE_16BIT,
                   I2S_CHANNEL_STEREO) != ESP_OK) {
        return false;
    }
    return codec_.initialized()
        ? codec_.configureSampleRate(sample_rate)
        : codec_.begin(sample_rate);
}

bool AudioService::writeMono(const int16_t * samples, size_t frames) {
    int16_t stereo[256];
    while(frames > 0) {
        const size_t chunk = frames < 128 ? frames : 128;
        for(size_t i = 0; i < chunk; ++i) {
            stereo[i * 2] = samples[i];
            stereo[i * 2 + 1] = samples[i];
        }
        size_t written = 0;
        const size_t bytes = chunk * 2 * sizeof(int16_t);
        if(i2s_write(kI2sPort, stereo, bytes, &written,
                     pdMS_TO_TICKS(250)) != ESP_OK || written != bytes) {
            return false;
        }
        samples += chunk;
        frames -= chunk;
    }
    return true;
}

bool AudioService::writeStereo(const int16_t * samples, size_t frames) {
    while(frames > 0) {
        const size_t chunk = frames < 128 ? frames : 128;
        const size_t bytes = chunk * 2 * sizeof(int16_t);
        size_t written = 0;
        if(i2s_write(kI2sPort, samples, bytes, &written,
                     pdMS_TO_TICKS(250)) != ESP_OK || written != bytes) {
            return false;
        }
        samples += chunk * 2;
        frames -= chunk;
    }
    return true;
}

bool AudioService::finishRecording() {
    if(!recording_file_) return true;
    uint8_t header[kWavHeaderBytes]{};
    const bool header_ready = buildWavHeader(
        header, sizeof(header), recording_data_bytes_, recording_sample_rate_, 1);
    const bool seek_ready = header_ready && recording_storage_ &&
        recording_storage_->seekManaged(recording_file_, 0);
    const bool write_ready = seek_ready &&
        recording_storage_->writeManaged(recording_file_, header, sizeof(header)) ==
            sizeof(header);
    recording_file_.flush();
    if(recording_storage_) recording_storage_->closeManaged(recording_file_);
    recording_storage_ = nullptr;
    recording_data_bytes_ = 0;
    recording_sample_rate_ = 0;
    return write_ready;
}

void AudioService::closeHardwareSession(AudioUse owner) {
    digitalWrite(kAmplifierPin, LOW);
    if(i2s_installed_) i2s_zero_dma_buffer(kI2sPort);
    codec_.sleep();
    arbiter_.release(owner);
}

bool AudioService::safeRecordingPath(const char * path) {
    static const char prefix[] = "/FireflyOS/Recordings/";
    if(!path || strncmp(path, prefix, sizeof(prefix) - 1) != 0 ||
       !endsWith(path, ".tmp") || strlen(path) >= 160 ||
       strchr(path, '\\') || strchr(path, ':')) {
        return false;
    }
    const char * tail = path + sizeof(prefix) - 1;
    return tail[0] && !strstr(tail, "../") && !strstr(tail, "/..") &&
           !strstr(tail, "//");
}

}  // namespace firefly
