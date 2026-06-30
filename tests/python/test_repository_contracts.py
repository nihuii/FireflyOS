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


if __name__ == "__main__":
    unittest.main()
