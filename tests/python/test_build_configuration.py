from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
FLASH_SIZE_BYTES = 0x2000000


def parse_partition_table(path):
    partitions = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) < 5:
            raise AssertionError(f"invalid partition row: {raw_line}")
        partitions.append(
            {
                "name": fields[0],
                "type": fields[1],
                "subtype": fields[2],
                "offset": int(fields[3], 0),
                "size": int(fields[4], 0),
            }
        )
    return partitions


class BuildConfigurationTests(unittest.TestCase):
    def test_sketch_owns_a_complete_non_overlapping_32mb_partition_table(self):
        partition_file = ROOT / "Firefly" / "partitions.csv"
        self.assertTrue(
            partition_file.is_file(),
            "Firefly/partitions.csv must pin the Arduino IDE partition layout",
        )

        partitions = parse_partition_table(partition_file)
        self.assertEqual(
            [partition["name"] for partition in partitions],
            ["nvs", "otadata", "app0", "app1", "ffat", "coredump"],
        )
        self.assertEqual(partitions[2]["subtype"], "ota_0")
        self.assertEqual(partitions[3]["subtype"], "ota_1")
        self.assertEqual(partitions[2]["size"], 0x480000)
        self.assertEqual(partitions[2]["size"], partitions[3]["size"])

        for current, following in zip(partitions, partitions[1:]):
            current_end = current["offset"] + current["size"]
            self.assertLessEqual(
                current_end,
                following["offset"],
                f"{current['name']} overlaps {following['name']}",
            )

        final = partitions[-1]
        self.assertEqual(final["offset"] + final["size"], FLASH_SIZE_BYTES)

    def test_partition_scheme_precedes_flash_size_in_cli_fqbn(self):
        script = (ROOT / "tools" / "build_firmware.ps1").read_text(
            encoding="utf-8"
        )
        partition_option = "PartitionScheme=app5M_fat24M_32MB"
        flash_option = "FlashSize=32M"
        self.assertIn(partition_option, script)
        self.assertIn(flash_option, script)
        self.assertLess(
            script.index(partition_option),
            script.index(flash_option),
            "PartitionScheme must follow ESP32 2.0.17 precedence rules",
        )


if __name__ == "__main__":
    unittest.main()
