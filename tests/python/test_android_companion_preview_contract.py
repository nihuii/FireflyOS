import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PREVIEW = ROOT / "docs" / "UI预览" / "04-Android伴侣"


class AndroidCompanionPreviewContractTests(unittest.TestCase):
    def read_required_file(self, name: str) -> str:
        path = PREVIEW / name
        self.assertTrue(path.is_file(), f"missing preview artifact: {path}")
        return path.read_text(encoding="utf-8")

    def test_android_companion_preview_covers_required_pages(self):
        preview = self.read_required_file("index.html")
        for token in (
            "FireflyOS Android Companion Preview",
            'id="scan-pair"',
            'id="device-home"',
            'id="notification-permission"',
            'id="settings-sync"',
            'id="theme-manager"',
            'id="weather-city"',
            'id="media-control"',
            'id="find-device"',
            'id="firmware-update"',
            'id="unbind"',
        ):
            self.assertIn(token, preview)

    def test_android_companion_preview_makes_offline_limits_explicit(self):
        preview = self.read_required_file("index.html")
        for token in (
            "离线可浏览最近设备信息",
            "远程操作已禁用",
            "等待 BLE 连接后可用",
            "手表本地时间、闹钟、媒体和活动继续独立运行",
            "disabled",
            "aria-disabled=\"true\"",
        ):
            self.assertIn(token, preview)

    def test_android_companion_approval_gate_records_user_approval(self):
        approval = self.read_required_file("审批记录.md")
        for token in (
            "状态：**已批准**",
            "2026-07-24",
            "用户已明确回复“批准”",
            "index.html",
            "设备扫描/配对",
            "通知授权",
            "解除绑定",
            "允许继续 Task 6",
            "允许继续 Task 7",
            "Task 6",
            "Task 7",
            "不包含提交、合并、推送、Task 8",
        ):
            self.assertIn(token, approval)


if __name__ == "__main__":
    unittest.main()
