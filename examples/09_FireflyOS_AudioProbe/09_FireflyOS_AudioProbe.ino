#include <Arduino.h>
#include <FireflyOS.h>
#include <Wire.h>

namespace {

firefly::I2cBusManager i2c_bus(Wire);
firefly::Es8311Device codec(i2c_bus);
firefly::AudioService audio(codec);
firefly::SdCardDevice sd_card;
firefly::StorageService storage;

const int16_t probe_tone[] = {
    0, 3212, 6392, 9512, 12539, 15446, 18204, 20787,
    23170, 25330, 27245, 28898, 30273, 31356, 32137, 32609,
    32767, 32609, 32137, 31356, 30273, 28898, 27245, 25330,
    23170, 20787, 18204, 15446, 12539, 9512, 6392, 3212,
    0, -3212, -6392, -9512, -12539, -15446, -18204, -20787,
    -23170, -25330, -27245, -28898, -30273, -31356, -32137, -32609,
    -32767, -32609, -32137, -31356, -30273, -28898, -27245, -25330,
    -23170, -20787, -18204, -15446, -12539, -9512, -6392, -3212,
};

bool recording = false;
uint32_t recording_started_at = 0;
const char * probe_path = "/FireflyOS/Recordings/AUDIO_PROBE.tmp";

}  // namespace

void setup() {
    Serial.begin(115200);
    Wire.begin(15, 14);
    storage.begin();
    const bool sd_ready = sd_card.begin();
    if(sd_ready) {
        storage.attachSd(sd_card.filesystem(), sd_card);
        firefly::AudioService::cleanupTemporaryRecordings(storage);
    }
    if(!audio.begin()) {
        Serial.println("AUDIO_PROBE codec_or_i2s_failed");
        return;
    }
    audio.setVolume(35);
    audio.playPcm(probe_tone,
                  sizeof(probe_tone) / sizeof(probe_tone[0]),
                  16000,
                  firefly::AudioUse::System);
    if(sd_ready && audio.startRecording(storage, probe_path, 16000)) {
        recording = true;
        recording_started_at = millis();
        Serial.println("AUDIO_PROBE recording_started");
    }
}

void loop() {
    if(!recording) {
        delay(20);
        return;
    }
    audio.serviceRecording();
    if(millis() - recording_started_at >= 2000) {
        audio.stop();
        recording = false;
        const bool replayed = audio.playWav(
            storage, probe_path, firefly::AudioUse::System);
        Serial.printf("AUDIO_PROBE replay=%s\n", replayed ? "ok" : "failed");
    }
}
