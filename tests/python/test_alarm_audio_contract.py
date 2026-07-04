from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class AlarmAudioContractTests(unittest.TestCase):
    def test_alarm_service_publishes_a_ui_consumed_system_event(self):
        event = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "core" / "SystemEvent.h"
        ).read_text(encoding="utf-8")
        alarm = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "AlarmService.h"
        ).read_text(encoding="utf-8")
        interaction = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("AlarmTriggered", event)
        self.assertIn("publishTrigger", alarm)
        self.assertIn("alarm_service.publishTrigger", interaction)
        self.assertIn("case firefly::EventType::AlarmTriggered", interaction)
        time_callback = interaction.split("void update_time_cb", 1)[1].split(
            "void enter_sleep_screen_mode", 1
        )[0]
        self.assertNotIn("trigger_alarm_alert(triggered_slot", time_callback)

    def test_four_bounded_builtin_ringtones_are_exposed(self):
        header = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "AlarmService.h"
        ).read_text(encoding="utf-8")
        source = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "AlarmService.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("struct AlarmToneResource", header)
        self.assertIn("kRingtoneCount = 4", header)
        self.assertIn("ringtoneResource", header)
        for name in ("Trailblaze", "Starglow", "Night Sky", "Classic Bell"):
            self.assertIn(name, source)
        self.assertIn("sample_rate = 16000", header)
        self.assertIn("kMaximumRingtoneFrames = 320000", header)

    def test_alarm_loop_is_non_blocking_and_dismiss_releases_audio(self):
        audio_header = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "AudioService.h"
        ).read_text(encoding="utf-8")
        interaction = (
            ROOT / "Firefly" / "FireflyInteraction.cpp"
        ).read_text(encoding="utf-8")
        sketch = (ROOT / "Firefly" / "Firefly.ino").read_text(
            encoding="utf-8"
        )
        self.assertIn("startLoopingPcm", audio_header)
        self.assertIn("void service()", audio_header)
        self.assertIn("audio_service.startLoopingPcm", interaction)
        self.assertIn('"Sound unavailable"', interaction)
        self.assertIn("audio_service.stop();", interaction)
        self.assertIn("audio_service.service();", sketch)


if __name__ == "__main__":
    unittest.main()
