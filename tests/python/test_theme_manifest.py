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


if __name__ == "__main__":
    unittest.main()
