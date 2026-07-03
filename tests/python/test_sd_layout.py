from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class SdLayoutTests(unittest.TestCase):
    def test_managed_directories_and_one_bit_pins_are_fixed(self):
        source = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "hal" /
            "SdCardDevice.cpp"
        ).read_text(encoding="utf-8")
        expected = {
            "Music", "Recordings", "Pictures", "Themes",
            "Updates", "Backups", "Logs",
        }
        directory_table = source.split(
            "const char * const kManagedDirectories[] = {", 1
        )[1].split("};", 1)[0]
        for directory in expected:
            self.assertIn(f'"/FireflyOS/{directory}"', directory_table)
        self.assertEqual(directory_table.count('"/FireflyOS/'), len(expected))
        self.assertIn("SD_MMC.setPins(kClockPin, kCommandPin, kDataPin)", source)
        self.assertIn('SD_MMC.begin("/sdcard", true, false', source)

    def test_runtime_publishes_removal_and_updates_capability(self):
        event = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "core" /
            "SystemEvent.h"
        ).read_text(encoding="utf-8")
        interaction = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(
            encoding="utf-8", errors="ignore"
        )
        state = (ROOT / "Firefly" / "FireflyState.cpp").read_text(
            encoding="utf-8", errors="ignore"
        )
        self.assertIn("SdRemoved", event)
        self.assertIn("firefly::SdCardDevice sd_card", state)
        self.assertIn("sd_card.takeRemovedEvent()", interaction)
        self.assertIn("Capability::Sd", interaction)


if __name__ == "__main__":
    unittest.main()
