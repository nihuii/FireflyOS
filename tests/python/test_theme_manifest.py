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


if __name__ == "__main__":
    unittest.main()
