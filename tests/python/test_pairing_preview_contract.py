import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PREVIEW = ROOT / "docs" / "UI预览" / "04-Android伴侣"


class PairingPreviewContractTests(unittest.TestCase):
    def test_watch_pairing_preview_covers_approval_states(self):
        preview = (PREVIEW / "watch-pairing.html").read_text(encoding="utf-8")
        for token in (
            "width:410px",
            "height:502px",
            "min-height:48px",
            'id="pairing-request"',
            'id="pairing-success"',
            'id="pairing-failed"',
            'id="unbind-confirm"',
            "482 731",
            "Pixel 9",
            "Allow",
            "Deny",
        ):
            self.assertIn(token, preview)

    def test_pairing_approval_records_user_decision(self):
        approval = (PREVIEW / "配对审批记录.md").read_text(encoding="utf-8")
        budget = (PREVIEW / "配对资源预算.md").read_text(encoding="utf-8")
        self.assertIn("状态：**已批准**", approval)
        self.assertIn("2026-07-06", approval)
        for token in ("410 × 502", "48px", "128-bit", "HMAC-SHA256", "不记录"):
            self.assertIn(token, approval + budget)


if __name__ == "__main__":
    unittest.main()
