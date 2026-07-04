from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "libraries" / "FireflyOS" / "src" / "firefly"


class StorageMediaBoundaryTests(unittest.TestCase):
    def read(self, relative):
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_storage_service_is_the_managed_sd_entry_point(self):
        header = self.read(
            "libraries/FireflyOS/src/firefly/services/StorageService.h"
        )
        for api in (
            "attachSd", "detachSd", "openManaged", "openNextManaged",
            "managedExists", "removeManaged", "renameManaged",
            "managedFileName", "managedFilePath", "managedFileSize",
            "managedFileIsDirectory",
            "reportSdResult", "sdAvailable",
        ):
            self.assertIn(api, header)

    def test_sd_failures_from_clients_reach_the_device_monitor(self):
        storage = self.read(
            "libraries/FireflyOS/src/firefly/services/StorageService.cpp"
        )
        device = self.read(
            "libraries/FireflyOS/src/firefly/hal/SdCardDevice.h"
        )
        self.assertIn("void noteIoResult(bool success)", device)
        self.assertIn("sd_device_->noteIoResult(success)", storage)

    def test_media_clients_do_not_keep_raw_filesystem_entry_points(self):
        clients = [
            "libraries/FireflyOS/src/firefly/apps/files/FilesApp.h",
            "libraries/FireflyOS/src/firefly/apps/music/MusicApp.h",
            "libraries/FireflyOS/src/firefly/apps/recorder/RecorderApp.h",
            "libraries/FireflyOS/src/firefly/apps/themes/ThemesApp.h",
            "libraries/FireflyOS/src/firefly/services/AudioService.h",
            "libraries/FireflyOS/src/firefly/services/ThemePackageService.h",
        ]
        for path in clients:
            source = self.read(path)
            self.assertNotIn("fs::FS *", source, path)
            self.assertNotIn("fs::FS &", source, path)

    def test_formal_runtime_does_not_bypass_storage_service(self):
        interaction = self.read("Firefly/FireflyInteraction.cpp")
        self.assertNotIn("SD_MMC", interaction)
        for app in ("files_app", "music_app", "recorder_app", "themes_app"):
            self.assertIn(f"{app}.bindStorage(storage_service", interaction)

    def test_media_clients_do_not_bypass_managed_file_metadata(self):
        clients = [
            ROOT / "libraries/FireflyOS/src/firefly/apps/music/MusicApp.cpp",
            ROOT / "libraries/FireflyOS/src/firefly/apps/themes/ThemesApp.cpp",
            ROOT / "libraries/FireflyOS/src/firefly/services/AudioService.cpp",
            ROOT / "libraries/FireflyOS/src/firefly/services/FileScanService.cpp",
            ROOT / "libraries/FireflyOS/src/firefly/services/ThemePackageService.cpp",
        ]
        for path in clients:
            source = path.read_text(encoding="utf-8")
            for direct_call in (".name()", ".path()", ".size()", ".isDirectory()"):
                self.assertNotIn(direct_call, source, str(path))


if __name__ == "__main__":
    unittest.main()
