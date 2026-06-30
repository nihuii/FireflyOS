#!/usr/bin/env python3
"""Extract the latest FireflyOS runtime baseline record from a serial log."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


BASELINE_PATTERN = re.compile(
    r"FIREFLY_BASELINE\s+"
    r"startup_ms=(?P<startup_ms>\d+)\s+"
    r"internal_free=(?P<internal_free>\d+)\s+"
    r"internal_min=(?P<internal_min>\d+)\s+"
    r"psram_free=(?P<psram_free>\d+)\s+"
    r"psram_size=(?P<psram_size>\d+)"
)


def parse_baseline_log(text: str) -> dict[str, int]:
    matches = list(BASELINE_PATTERN.finditer(text))
    if not matches:
        raise ValueError("serial log does not contain a complete FIREFLY_BASELINE record")
    return {name: int(value) for name, value in matches[-1].groupdict().items()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("serial_log", type=Path, help="UTF-8 serial log file")
    args = parser.parse_args()

    record = parse_baseline_log(
        args.serial_log.read_text(encoding="utf-8", errors="replace")
    )
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
