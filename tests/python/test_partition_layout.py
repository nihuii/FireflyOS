import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "tools" / "validate_partition_layout.py"
PARTITIONS = ROOT / "Firefly" / "partitions.csv"


def load_validator():
    if not VALIDATOR.is_file():
        raise AssertionError("partition validator must exist")
    spec = importlib.util.spec_from_file_location("partition_validator", VALIDATOR)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class PartitionLayoutTests(unittest.TestCase):
    def test_non_release_sketches_own_stable_partition_copies(self):
        expected = (
            ROOT / "tools" / "partitions" / "app5M_fat24M_32MB.csv"
        ).read_text(encoding="utf-8").splitlines()
        for relative in (
            Path("tests/FireflyCoreTests/partitions.csv"),
            Path("examples/09_FireflyOS_AudioProbe/partitions.csv"),
        ):
            path = ROOT / relative
            self.assertTrue(path.is_file(), relative.as_posix())
            self.assertEqual(
                expected,
                path.read_text(encoding="utf-8").splitlines(),
                relative.as_posix(),
            )

    def test_formal_build_validates_source_binary_and_slot_budget(self):
        script = (ROOT / "tools" / "build_firmware.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("Firefly\\partitions.csv", script)
        self.assertIn("validate_partition_layout.py", script)
        self.assertIn("Firefly.ino.partitions.bin", script)
        self.assertIn("9227468", script)
        self.assertIn("upload.maximum_size=11534336", script)
        self.assertLess(
            script.index("--csv $partitionSource"),
            script.index("& $arduinoCli compile"),
        )
        self.assertGreater(
            script.index("Firefly.ino.partitions.bin"),
            script.index("& $arduinoCli compile"),
        )

    def test_firefly_layout_has_equal_11mb_ota_slots(self):
        self.assertTrue(PARTITIONS.is_file(), "formal Firefly partition CSV must exist")
        validator = load_validator()
        rows = validator.parse_csv(PARTITIONS)
        validator.validate_firefly_layout(rows)
        by_name = {row.name: row for row in rows}
        self.assertEqual(by_name["otadata"].size, 0x2000)
        self.assertEqual(by_name["app0"].subtype, "ota_0")
        self.assertEqual(by_name["app1"].subtype, "ota_1")
        self.assertEqual(by_name["app0"].size, 0xB00000)
        self.assertEqual(by_name["app1"].size, 0xB00000)
        self.assertLessEqual(max(row.offset + row.size for row in rows), 0x2000000)

    def test_validator_rejects_overlap_and_wrong_slot_size(self):
        validator = load_validator()
        valid = validator.parse_csv(PARTITIONS)
        overlap = list(valid)
        overlap[-1] = validator.Partition(
            overlap[-1].name,
            overlap[-1].type,
            overlap[-1].subtype,
            valid[-2].offset,
            overlap[-1].size,
            overlap[-1].flags,
        )
        with self.assertRaisesRegex(ValueError, "overlap"):
            validator.validate_firefly_layout(overlap)

        wrong_slot = list(valid)
        app0_index = next(i for i, row in enumerate(wrong_slot) if row.name == "app0")
        app0 = wrong_slot[app0_index]
        wrong_slot[app0_index] = validator.Partition(
            app0.name, app0.type, app0.subtype,
            app0.offset, app0.size - 0x1000, app0.flags,
        )
        with self.assertRaisesRegex(ValueError, "11MB"):
            validator.validate_firefly_layout(wrong_slot)

    def test_binary_layout_must_match_csv(self):
        validator = load_validator()
        source = validator.parse_csv(PARTITIONS)
        subtype_values = {
            ("data", "nvs"): (1, 2),
            ("data", "ota"): (1, 0),
            ("data", "spiffs"): (1, 0x82),
            ("app", "ota_0"): (0, 0x10),
            ("app", "ota_1"): (0, 0x11),
        }
        encoded = bytearray()
        for row in source:
            kind, subtype = subtype_values[(row.type, row.subtype)]
            label = row.name.encode("ascii").ljust(16, b"\0")
            encoded.extend(struct.pack(
                "<HBBII16sI", 0x50AA, kind, subtype,
                row.offset, row.size, label, row.flags,
            ))
        encoded.extend(b"\xff" * 32)
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "partitions.bin"
            binary.write_bytes(encoded)
            compiled = validator.parse_binary(binary)
        validator.compare_layouts(source, compiled)

        altered = list(compiled)
        app1 = altered[3]
        altered[3] = validator.Partition(
            app1.name, app1.type, app1.subtype,
            app1.offset + 0x1000, app1.size, app1.flags,
        )
        with self.assertRaisesRegex(ValueError, "differs"):
            validator.compare_layouts(source, altered)


if __name__ == "__main__":
    unittest.main()
