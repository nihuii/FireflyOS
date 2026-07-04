from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
APPS = ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "apps"


class MediaAppsContractTests(unittest.TestCase):
    def read(self, relative):
        return (APPS / relative).read_text(encoding="utf-8")

    def test_files_app_is_bounded_and_deletion_is_scoped(self):
        header = self.read(Path("files") / "FilesApp.h")
        source = self.read(Path("files") / "FilesApp.cpp")
        self.assertIn("kPageSize = 32", header)
        self.assertIn("char name[48]", header)
        self.assertIn("kManagedDirectoryCount = 7", header)
        self.assertIn("canDeleteFile", header)
        for directory in (
            "Music", "Recordings", "Pictures", "Themes", "Updates",
            "Backups", "Logs",
        ):
            self.assertIn(f'"{directory}"', source)
        self.assertNotIn("recursive", source.lower())

    def test_files_page_scan_runs_off_ui_and_returns_an_event(self):
        scanner_header_path = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "FileScanService.h"
        )
        scanner_source_path = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "FileScanService.cpp"
        )
        self.assertTrue(scanner_header_path.exists(), "FileScanService.h missing")
        self.assertTrue(scanner_source_path.exists(), "FileScanService.cpp missing")
        if not scanner_header_path.exists() or not scanner_source_path.exists():
            return
        scanner_header = scanner_header_path.read_text(encoding="utf-8")
        scanner_source = scanner_source_path.read_text(encoding="utf-8")
        files = self.read(Path("files") / "FilesApp.cpp")
        interaction = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(
            encoding="utf-8"
        )
        for contract in (
            "class FileScanService", "kPageSize = 32",
            "kEntriesPerTick = 4", "FileScanPage", "takeResult",
        ):
            self.assertIn(contract, scanner_header)
        self.assertNotIn("lv_", scanner_header + scanner_source)
        self.assertIn("EventType::FilesPageReady", scanner_source)
        self.assertIn("file_scan_service.service", interaction)
        self.assertIn("case firefly::EventType::FilesPageReady", interaction)
        self.assertIn("onPageReady", files)
        self.assertNotIn("scanStep", files)

    def test_music_app_has_fixed_queue_and_power_saver_tick(self):
        header = self.read(Path("music") / "MusicApp.h")
        self.assertIn("kMaxTracks = 128", header)
        self.assertIn("char path[96]", header)
        self.assertIn("onSdRemoved", header)
        self.assertIn("tick(uint32_t now_ms, bool power_saver)", header)
        self.assertIn("index_limit_reached_", header)

    def test_recorder_names_and_lifecycle_are_bounded(self):
        header = self.read(Path("recorder") / "RecorderApp.h")
        source = self.read(Path("recorder") / "RecorderApp.cpp")
        self.assertIn("kMinimumFreeBytes = 2UL * 1024UL * 1024UL", header)
        self.assertIn("makeRecordingName", header)
        self.assertIn("REC_%04d%02d%02d_%02d%02d%02d.wav", source)
        self.assertIn("REC_%06lu.wav", source)
        self.assertIn(".tmp", source)
        self.assertIn("RECORDING", source)

    def test_themes_app_uses_validated_import_and_releases_preview(self):
        header = self.read(Path("themes") / "ThemesApp.h")
        source = self.read(Path("themes") / "ThemesApp.cpp")
        package_source = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "ThemePackageService.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("ThemePackageService", header)
        self.assertIn("releasePreview", header)
        self.assertIn("importSelected", header)
        self.assertIn("importPackage", source)
        self.assertIn("kMaxIconPackBytes", package_source)
        self.assertIn("takeAppliedPalette", header)
        self.assertIn("issue.resource", source)
        self.assertIn("issue.actual", source)
        self.assertIn("issue.limit", source)

    def test_formal_firmware_routes_to_real_media_apps(self):
        umbrella = (
            ROOT / "libraries" / "FireflyOS" / "src" / "FireflyOS.h"
        ).read_text(encoding="utf-8")
        sketch = (ROOT / "Firefly" / "Firefly.ino").read_text(
            encoding="utf-8"
        )
        state = (ROOT / "Firefly" / "FireflyState.cpp").read_text(
            encoding="utf-8"
        )
        for app in ("FilesApp", "MusicApp", "RecorderApp", "ThemesApp"):
            self.assertIn(app, umbrella)
            self.assertIn(app, state)
        for call in (
            "files_app.show()", "music_app.show()", "recorder_app.show()",
            "themes_app.show()",
        ):
            self.assertIn(call, sketch)

    def test_runtime_keeps_alarm_and_recording_safety_boundaries(self):
        audio = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "AudioService.h"
        ).read_text(encoding="utf-8")
        music = self.read(Path("music") / "MusicApp.cpp")
        interaction = (
            ROOT / "Firefly" / "FireflyInteraction.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("discardPlayback", audio)
        self.assertIn("discardPlayback", music)
        self.assertIn("last_storage_probe_at", interaction)
        self.assertIn("if(recorder_app.recording())", interaction)
        self.assertIn("themes_app.takeAppliedPalette", interaction)


if __name__ == "__main__":
    unittest.main()
