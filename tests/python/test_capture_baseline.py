import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "capture_baseline.py"


def load_module():
    spec = importlib.util.spec_from_file_location("capture_baseline", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CaptureBaselineTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(MODULE_PATH.is_file(), "capture_baseline.py must exist")

    def test_parses_latest_complete_baseline_record(self):
        module = load_module()
        text = """
booting
FIREFLY_BASELINE startup_ms=1250 internal_free=210000 internal_min=198000 psram_free=7010000 psram_size=8388608
"""
        self.assertEqual(
            module.parse_baseline_log(text),
            {
                "startup_ms": 1250,
                "internal_free": 210000,
                "internal_min": 198000,
                "psram_free": 7010000,
                "psram_size": 8388608,
            },
        )

    def test_rejects_log_without_baseline_record(self):
        module = load_module()
        with self.assertRaisesRegex(ValueError, "FIREFLY_BASELINE"):
            module.parse_baseline_log("ordinary serial output")


if __name__ == "__main__":
    unittest.main()
