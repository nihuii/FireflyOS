from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RepositoryContracts(unittest.TestCase):
    def test_main_sketch_exists(self):
        self.assertTrue((ROOT / "Firefly" / "Firefly.ino").is_file())

    def test_lvgl_configuration_exposes_image_cache_setting(self):
        text = (ROOT / "libraries" / "lv_conf.h").read_text(
            encoding="utf-8", errors="ignore"
        )
        self.assertIn("LV_IMG_CACHE_DEF_SIZE", text)

    def test_baseline_document_has_required_metrics(self):
        baseline = ROOT / "docs" / "模块说明" / "00-当前基线.md"
        self.assertTrue(baseline.is_file(), "baseline document must exist")
        text = baseline.read_text(encoding="utf-8")
        for name in ("固件大小", "内部 SRAM", "PSRAM", "启动时间", "400mAh"):
            self.assertIn(name, text)

    def test_main_sketch_emits_baseline_resource_log(self):
        text = (ROOT / "Firefly" / "Firefly.ino").read_text(
            encoding="utf-8", errors="ignore"
        )
        self.assertIn("FIREFLY_BASELINE", text)
        for field in ("startup_ms", "internal_free", "internal_min", "psram_free", "psram_size"):
            self.assertIn(field, text)


if __name__ == "__main__":
    unittest.main()
