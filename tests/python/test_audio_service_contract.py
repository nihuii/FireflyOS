from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class AudioServiceContractTests(unittest.TestCase):
    def test_audio_hardware_pins_and_low_power_policy_are_fixed(self):
        codec = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "hal" /
            "Es8311Device.h"
        ).read_text(encoding="utf-8")
        header = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "AudioService.h"
        ).read_text(encoding="utf-8")
        source = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "AudioService.cpp"
        ).read_text(encoding="utf-8")
        for declaration in (
            "kMclkPin = 41", "kBclkPin = 45", "kWordSelectPin = 40",
            "kDataOutPin = 42", "kDataInPin = 16", "kAmplifierPin = 46",
        ):
            self.assertIn(declaration, header)
        self.assertIn("Es8311ControlAdapter", codec)
        self.assertIn("digitalWrite(kAmplifierPin, LOW)", source)
        self.assertIn("codec_.sleep()", source)
        self.assertNotIn("lv_", header + source)

    def test_probe_and_wav_recovery_contract_exist(self):
        probe = (
            ROOT / "examples" / "09_FireflyOS_AudioProbe" /
            "09_FireflyOS_AudioProbe.ino"
        ).read_text(encoding="utf-8")
        service = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "AudioService.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("playPcm", probe)
        self.assertIn("startRecording", probe)
        self.assertIn("buildWavHeader", service)
        self.assertIn("cleanupTemporaryRecordings", service)
        self.assertIn("I2S_BITS_PER_SAMPLE_16BIT", service)
        build_script = (ROOT / "tools" / "build_firmware.ps1").read_text(
            encoding="utf-8"
        )
        verify_script = (ROOT / "tools" / "verify_all.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("AudioProbe", build_script)
        self.assertIn("-Target AudioProbe", verify_script)

    def test_formal_firmware_cleans_temporary_recordings_after_sd_mount(self):
        firmware = (ROOT / "Firefly" / "Firefly.ino").read_text(
            encoding="utf-8"
        )
        interaction = (
            ROOT / "Firefly" / "FireflyInteraction.cpp"
        ).read_text(encoding="utf-8")
        cleanup_call = "AudioService::cleanupTemporaryRecordings(storage_service)"
        self.assertIn(cleanup_call, firmware)
        self.assertIn(cleanup_call, interaction)


if __name__ == "__main__":
    unittest.main()
