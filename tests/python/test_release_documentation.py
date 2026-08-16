import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        raise AssertionError(f"missing Plan 6 release artifact: {relative}")
    return path.read_text(encoding="utf-8")


class ReleaseDocumentationTests(unittest.TestCase):
    def test_release_documents_exist_and_keep_hardware_pending(self):
        ota = read("docs/模块说明/10-OTA发布规范.md")
        report = read("docs/模块说明/11-最终验收报告.md")
        checklist = read("docs/模块说明/12-发布清单.md")

        for token in (
            "0x10000",
            "0xB10000",
            "0xB00000",
            "9,227,468",
            "FIREFLY_SIGNING_KEY",
            "FIREFLY_RELEASE_BUILD",
            "SD",
            "HTTPS",
            "回滚",
        ):
            self.assertIn(token, ota)
        for token in (
            "24 小时",
            "400mAh",
            "断电 OTA",
            "Gate E",
            "PENDING",
        ):
            self.assertIn(token, report)
        self.assertNotIn("v1.0.0-rc1 已创建", ota + report + checklist)
        self.assertIn("禁止创建 RC 标签", checklist)

    def test_existing_docs_describe_plan6_as_software_not_hardware_pass(self):
        intro = read("docs/项目介绍.md")
        readme = read("Firefly/README.md")
        architecture = read("docs/FireflyOS系统架构总纲.md")
        stale = "天气和诊断应用的实际业务页面（桌面入口仍为占位）"
        self.assertNotIn(stale, intro)
        for text in (intro, readme, architecture):
            self.assertIn("计划 6", text)
            self.assertIn("PENDING", text)

    def test_release_build_is_fail_closed_and_formal_verification_checks_docs(self):
        build = read("tools/build_firmware.ps1")
        verify = read("tools/verify_all.ps1")
        ignore = read(".gitignore")
        for token in (
            "[ValidateSet('Development', 'Release')]",
            "FIREFLY_RELEASE_BUILD=1",
            "FireflyUpdatePublicKey.local.h",
            "FireflyUpdateConfig.local.h",
        ):
            self.assertIn(token, build)
        self.assertIn("tests.python.test_release_documentation", verify)
        self.assertIn("FireflyUpdatePublicKey.local.h", ignore)
        self.assertIn("FireflyUpdateConfig.local.h", ignore)

    def test_factory_reset_copy_matches_managed_allow_list(self):
        runtime = read("Firefly/Firefly.ino")
        preview = read("docs/UI预览/05-天气与更新/恢复出厂.html")
        module = read("docs/模块说明/09-WiFi天气与传输.md")
        self.assertIn("Delete managed FireflyOS data", runtime)
        self.assertIn("Delete managed FireflyOS data", preview)
        for token in (
            "Music",
            "Recordings",
            "Pictures",
            "Themes",
            "Updates",
            "Backups",
            "Logs",
            "未知",
        ):
            self.assertIn(token, module)


if __name__ == "__main__":
    unittest.main()
