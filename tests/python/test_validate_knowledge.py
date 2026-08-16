import importlib.util
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_PATH = ROOT / "knowledge" / "validate_knowledge.py"
SPEC = importlib.util.spec_from_file_location("validate_knowledge", VALIDATOR_PATH)
validator = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(validator)


class KnowledgePlanValidationTests(unittest.TestCase):
    def _copy_plans(self, destination: Path) -> None:
        source = ROOT / "docs" / "执行计划"
        for name in validator.PLAN_FILES:
            shutil.copyfile(source / name, destination / name)

    def _validate(self, plan_dir: Path) -> list[str]:
        original_plan_dir = validator.PLAN_DIR
        validator.PLAN_DIR = plan_dir
        errors: list[str] = []
        try:
            with mock.patch.object(validator, "check_plan_links"):
                validator.check_execution_plans(errors)
        finally:
            validator.PLAN_DIR = original_plan_dir
        return errors

    def test_each_task_requires_its_own_verification_command(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            plan_dir = Path(temporary_directory)
            self._copy_plans(plan_dir)
            plan_path = plan_dir / "01A-计划1缺口补全.md"
            text = plan_path.read_text(encoding="utf-8")
            text = text.replace(
                "- [ ] **验证命令：** 运行核心测试、Python 契约和主固件构建。",
                "- [ ] 运行核心测试、Python 契约和主固件构建。",
                1,
            )
            plan_path.write_text(text, encoding="utf-8")

            errors = self._validate(plan_dir)

        self.assertTrue(
            any("Task 2" in error and "验证命令" in error for error in errors),
            errors,
        )

    def test_last_task_does_not_borrow_markers_from_later_sections(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            plan_dir = Path(temporary_directory)
            self._copy_plans(plan_dir)
            plan_path = plan_dir / "01A-计划1缺口补全.md"
            text = plan_path.read_text(encoding="utf-8")
            text = text.replace(
                "- [ ] **验证命令：** 运行完整自动验证并在可用设备上采集串口日志。",
                "- [ ] 运行完整自动验证并在可用设备上采集串口日志。",
                1,
            )
            text = text.replace(
                "## 6. 自动验证",
                "## 6. 自动验证\n\n- **验证命令：** 这里只是章节级说明。",
                1,
            )
            plan_path.write_text(text, encoding="utf-8")

            errors = self._validate(plan_dir)

        self.assertTrue(
            any("Task 3" in error and "验证命令" in error for error in errors),
            errors,
        )

    def test_stale_main_sha_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            plan_dir = Path(temporary_directory)
            self._copy_plans(plan_dir)
            plan_path = plan_dir / "00-总体执行路线图.md"
            text = plan_path.read_text(encoding="utf-8")
            text = text.replace(
                "main@fab5509",
                "main@fab5509；旧 main 为 351dff7",
                1,
            )
            plan_path.write_text(text, encoding="utf-8")

            errors = self._validate(plan_dir)

        self.assertTrue(any("351dff7" in error for error in errors), errors)

    def test_full_current_main_sha_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            plan_dir = Path(temporary_directory)
            self._copy_plans(plan_dir)
            plan_path = plan_dir / "00-总体执行路线图.md"
            text = plan_path.read_text(encoding="utf-8")
            text = text.replace(
                "main@fab5509",
                "main@fab5509b94f17b37d9d10233d10660c979ee3507",
                1,
            )
            plan_path.write_text(text, encoding="utf-8")

            errors = self._validate(plan_dir)

        self.assertFalse(
            any(
                "stale main SHA" in error or "missing current baseline" in error
                for error in errors
            ),
            errors,
        )

    def test_ahead_count_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            plan_dir = Path(temporary_directory)
            self._copy_plans(plan_dir)
            plan_path = plan_dir / "00-总体执行路线图.md"
            text = plan_path.read_text(encoding="utf-8")
            text = text.replace("main@fab5509", "main@fab5509，ahead 15", 1)
            plan_path.write_text(text, encoding="utf-8")

            errors = self._validate(plan_dir)

        self.assertTrue(any("ahead 15" in error for error in errors), errors)


if __name__ == "__main__":
    unittest.main()
