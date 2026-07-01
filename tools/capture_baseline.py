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

GATE_A_PATTERN = re.compile(
    r"FIREFLY_GATE_A\s+"
    r"uptime_ms=(?P<uptime_ms>\d+)\s+"
    r"internal_free=(?P<internal_free>\d+)\s+"
    r"internal_min=(?P<internal_min>\d+)\s+"
    r"psram_free=(?P<psram_free>\d+)\s+"
    r"event_post_failures=(?P<event_post_failures>\d+)\s+"
    r"event_queue=(?P<event_queue>\d+)\s+"
    r"desktop_transition_max_ms=(?P<desktop_transition_max_ms>\d+)"
)


def parse_baseline_log(text: str) -> dict[str, int]:
    matches = list(BASELINE_PATTERN.finditer(text))
    if not matches:
        raise ValueError("serial log does not contain a complete FIREFLY_BASELINE record")
    return {name: int(value) for name, value in matches[-1].groupdict().items()}


def parse_gate_a_log(text: str) -> dict[str, int]:
    matches = list(GATE_A_PATTERN.finditer(text))
    if not matches:
        raise ValueError("serial log does not contain a complete FIREFLY_GATE_A record")
    return {name: int(value) for name, value in matches[-1].groupdict().items()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("serial_log", type=Path, help="UTF-8 serial log file")
    parser.add_argument(
        "--gate-a",
        action="store_true",
        help="extract the latest recurring FIREFLY_GATE_A diagnostic",
    )
    args = parser.parse_args()

    text = args.serial_log.read_text(encoding="utf-8", errors="replace")
    record = parse_gate_a_log(text) if args.gate_a else parse_baseline_log(text)
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
