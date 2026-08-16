from pathlib import Path
import json
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ThemeManifestTests(unittest.TestCase):
    def test_documented_manifest_is_valid_and_complete(self):
        document = (
            ROOT / "docs" / "模块说明" / "06-主题包格式.md"
        ).read_text(encoding="utf-8")
        block = re.search(r"```json\s*(\{.*?\})\s*```", document, re.S)
        self.assertIsNotNone(block)
        manifest = json.loads(block.group(1))
        self.assertEqual(manifest["schema"], 1)
        self.assertRegex(manifest["id"], r"^[a-z0-9-]{1,23}$")
        self.assertEqual(
            set(manifest["palette"]),
            {"bg_base", "bg_surface", "primary", "secondary", "critical"},
        )
        for color in manifest["palette"].values():
            self.assertRegex(color, r"^#[0-9A-Fa-f]{6}$")
        self.assertTrue(manifest["wallpaper"].endswith(".rgb565"))
        self.assertGreater(manifest.get("wallpaper_width", 0), 0)
        self.assertGreater(manifest.get("wallpaper_height", 0), 0)
        self.assertLessEqual(manifest.get("wallpaper_width", 0), 410)
        self.assertLessEqual(manifest.get("wallpaper_height", 0), 502)
        self.assertTrue(manifest["glance"].endswith(".png"))
        for resource in (
            manifest["wallpaper"], manifest["glance"], manifest["icon_pack"]
        ):
            self.assertFalse(resource.startswith("/"))
            self.assertNotIn("..", resource.split("/"))
            self.assertNotIn("\\", resource)

    def test_service_has_bounded_validation_and_atomic_activation(self):
        header = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "ThemePackageService.h"
        ).read_text(encoding="utf-8")
        source = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "ThemePackageService.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("kMaxManifestBytes = 2048", header)
        self.assertIn("kMaxIconPackBytes = 512 * 1024", header)
        self.assertIn("isSafeResourcePath", header + source)
        self.assertIn("validatePng", source)
        self.assertLess(
            source.index("storage.saveThemeCache"),
            source.index("storage.saveSettings"),
        )
        self.assertIn("FIREFLYOS_INCLUDE_FIREFLY_THEME", source)

    def test_wallpaper_dimensions_and_specific_validation_issue_are_required(self):
        header = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "ThemePackageService.h"
        ).read_text(encoding="utf-8")
        source = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "services" / "ThemePackageService.cpp"
        ).read_text(encoding="utf-8")
        app = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "apps" / "themes" / "ThemesApp.cpp"
        ).read_text(encoding="utf-8")
        for declaration in (
            "wallpaper_width", "wallpaper_height",
            "struct ThemeValidationIssue", "char resource[64]",
            "uint32_t actual", "uint32_t limit", "validatePackage",
        ):
            self.assertIn(declaration, header)
        self.assertIn("expected_wallpaper_bytes", source)
        self.assertIn("issue.resource", app)
        self.assertIn("issue.actual", app)
        self.assertIn("issue.limit", app)

    def test_runtime_theme_restyles_the_existing_control_tree(self):
        components_header = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "ui" / "UiComponents.h"
        ).read_text(encoding="utf-8")
        components_source = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "ui" / "UiComponents.cpp"
        ).read_text(encoding="utf-8")
        theme = (
            ROOT / "libraries" / "FireflyOS" / "src" / "firefly" /
            "ui" / "UiTheme.h"
        ).read_text(encoding="utf-8")
        runtime = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("fromPalette", theme)
        self.assertIn("setRuntime", theme)
        self.assertIn("applyThemeTree", components_header)
        self.assertIn("lv_obj_get_child_cnt", components_source)
        self.assertIn("UiComponents::applyThemeTree", runtime)

    def test_system_default_is_neutral_and_legacy_alias_is_migrated(self):
        services = ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "services"
        theme = (
            (services / "ThemePackageService.h").read_text(encoding="utf-8")
            + (services / "ThemePackageService.cpp").read_text(encoding="utf-8")
        )
        storage = (
            (services / "StorageService.h").read_text(encoding="utf-8")
            + (services / "StorageService.cpp").read_text(encoding="utf-8")
        )
        runtime = (ROOT / "Firefly" / "FireflyInteraction.cpp").read_text(
            encoding="utf-8"
        )
        companion = (services / "CompanionSyncService.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn('"system-default", "Default"', theme)
        self.assertIn('char theme_id[24] = "system-default"', storage)
        self.assertIn('strcmp(settings.theme_id, "firefly-default") == 0', storage)
        self.assertIn('strlcpy(settings.theme_id, "system-default"', storage)
        self.assertIn("saveSettings(settings)", storage)
        migration = storage[
            storage.index('strcmp(settings.theme_id, "firefly-default") == 0'):
            storage.index("if(settings.volume > 100)")
        ]
        self.assertNotIn("clearThemeCache", migration)
        self.assertIn("normalize_theme_id", runtime)
        self.assertIn('strcmp(theme_id, "firefly-default") == 0', runtime)
        self.assertIn("normalizeLegacyThemeAlias", companion)
        self.assertIn("persistence_->saveSnapshot(normalized)", companion)
        self.assertIn("companion_sync_service.settingsSnapshot()", runtime)
        cache_migration = storage[
            storage.index("bool StorageService::loadThemeCache"):
            storage.index("bool StorageService::clearThemeCache")
        ]
        self.assertIn('cached == "firefly-default"', cache_migration)
        self.assertIn('saveThemeCache("system-default", palette)', cache_migration)

        android_layout = (
            ROOT / "AndroidCompanion" / "app" / "src" / "main" / "res" /
            "layout" / "activity_main.xml"
        ).read_text(encoding="utf-8")
        theme_doc = (ROOT / "docs" / "模块说明" / "06-主题包格式.md").read_text(
            encoding="utf-8"
        )
        self.assertIn('android:text="system-default"', android_layout)
        self.assertIn("`system-default`", theme_doc)

    def test_core_layers_do_not_reference_role_art_paths(self):
        source_root = ROOT / "libraries" / "FireflyOS" / "src" / "firefly"
        forbidden = (
            "LockWallpaper.h", "SettingsWallpaper.h", "SleepIcons.h",
            "lock_wallpaper_firefly", "settings_wallpaper_firefly",
            "sleep_icon_firefly", "image/图片生成提示词",
        )
        offenders = []
        for folder in ("core", "protocol", "hal", "services"):
            for path in (source_root / folder).rglob("*"):
                if path.suffix not in (".h", ".cpp"):
                    continue
                text = path.read_text(encoding="utf-8")
                if any(token in text for token in forbidden):
                    offenders.append(str(path.relative_to(ROOT)))
        self.assertEqual([], offenders)


if __name__ == "__main__":
    unittest.main()
