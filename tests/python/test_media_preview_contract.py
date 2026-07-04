from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
PREVIEW = ROOT / "docs" / "UI预览" / "03-媒体应用"


class MediaPreviewContractTests(unittest.TestCase):
    def test_preview_covers_required_media_states(self):
        html = (PREVIEW / "index.html").read_text(encoding="utf-8")
        for state in (
            "files-normal", "sd-empty", "sd-removed", "music-playing",
            "recorder-active", "recorder-save-failed", "theme-preview",
            "theme-import-failed",
        ):
            self.assertIn(f'id="{state}"', html)
        self.assertIn("min-height:48px", html)
        self.assertIn("410px", html)
        self.assertIn("502px", html)

    def test_budget_and_approval_records_exist(self):
        budget = (PREVIEW / "资源预算.md").read_text(encoding="utf-8")
        approval = (PREVIEW / "审批记录.md").read_text(encoding="utf-8")
        self.assertIn("512KB", budget)
        self.assertIn("128", budget)
        self.assertIn("32", budget)
        self.assertIn("视觉终验", approval)


if __name__ == "__main__":
    unittest.main()
