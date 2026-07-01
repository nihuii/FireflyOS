from pathlib import Path
import importlib.util
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "assets" / "collect_system_glyphs.py"


class CollectSystemGlyphsTests(unittest.TestCase):
    def load_module(self):
        spec = importlib.util.spec_from_file_location("collect_system_glyphs", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_collects_cjk_and_preserves_seed_in_stable_order(self):
        module = self.load_module()
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            root = Path(temp)
            (root / "screen.cpp").write_text(
                'label("流萤"); // 重复：流\nlabel("设置");', encoding="utf-8"
            )
            (root / "ignore.bin").write_bytes("不扫描".encode("utf-8"))
            glyphs = module.collect_glyphs(root, "星穹")
        self.assertEqual(glyphs, "星穹流萤重复设置")

    def test_write_output_is_deterministic(self):
        module = self.load_module()
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            root = Path(temp)
            source = root / "ui.h"
            output = root / "glyphs.txt"
            source.write_text('const char * text = "锁屏";', encoding="utf-8")
            module.write_glyphs(root, output, "流萤")
            first = output.read_text(encoding="utf-8")
            module.write_glyphs(root, output, "流萤")
            self.assertEqual(output.read_text(encoding="utf-8"), first)
            self.assertEqual(first, "流萤锁屏\n")


if __name__ == "__main__":
    unittest.main()
