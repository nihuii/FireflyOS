#!/usr/bin/env python3
"""Validate FireflyOS's source and compiled ESP32 partition layouts."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
import struct
from typing import Sequence


PARTITION_MAGIC = 0x50AA
FLASH_SIZE = 0x2000000
OTA_SLOT_SIZE = 0xB00000


@dataclass(frozen=True)
class Partition:
    name: str
    type: str
    subtype: str
    offset: int
    size: int
    flags: int = 0


def _parse_number(value: str) -> int:
    text = value.strip()
    if not text:
        return 0
    return int(text, 0)


def parse_csv(path: Path) -> list[Partition]:
    partitions: list[Partition] = []
    with Path(path).open("r", encoding="utf-8", newline="") as source:
        for raw in csv.reader(source):
            if not raw or not raw[0].strip() or raw[0].lstrip().startswith("#"):
                continue
            cells = [cell.strip() for cell in raw]
            if len(cells) < 5:
                raise ValueError("partition row must contain at least five fields")
            partitions.append(Partition(
                name=cells[0],
                type=cells[1],
                subtype=cells[2],
                offset=_parse_number(cells[3]),
                size=_parse_number(cells[4]),
                flags=_parse_number(cells[5]) if len(cells) > 5 else 0,
            ))
    return partitions


def parse_binary(path: Path) -> list[Partition]:
    type_names = {0: "app", 1: "data"}
    subtype_names = {
        (0, 0x10): "ota_0",
        (0, 0x11): "ota_1",
        (1, 0x00): "ota",
        (1, 0x02): "nvs",
        (1, 0x82): "spiffs",
    }
    payload = Path(path).read_bytes()
    partitions: list[Partition] = []
    for offset in range(0, len(payload), 32):
        chunk = payload[offset:offset + 32]
        if len(chunk) < 32 or chunk == b"\xff" * 32:
            break
        magic, kind, subtype, address, size, label, flags = struct.unpack(
            "<HBBII16sI", chunk
        )
        if magic != PARTITION_MAGIC:
            break
        if kind not in type_names or (kind, subtype) not in subtype_names:
            raise ValueError(
                f"unsupported compiled partition type/subtype: {kind:#x}/{subtype:#x}"
            )
        name = label.split(b"\0", 1)[0].decode("ascii")
        partitions.append(Partition(
            name=name,
            type=type_names[kind],
            subtype=subtype_names[(kind, subtype)],
            offset=address,
            size=size,
            flags=flags,
        ))
    if not partitions:
        raise ValueError("compiled partition table contains no entries")
    return partitions


def validate_firefly_layout(rows: Sequence[Partition]) -> None:
    if not rows:
        raise ValueError("partition table is empty")
    by_name = {row.name: row for row in rows}
    if len(by_name) != len(rows):
        raise ValueError("duplicate partition name")
    required = {"nvs", "otadata", "app0", "app1", "littlefs"}
    missing = sorted(required.difference(by_name))
    if missing:
        raise ValueError(f"missing required partitions: {', '.join(missing)}")

    ordered = sorted(rows, key=lambda row: row.offset)
    for row in ordered:
        if row.offset < 0 or row.size <= 0:
            raise ValueError(f"invalid partition range: {row.name}")
    for left, right in zip(ordered, ordered[1:]):
        if left.offset + left.size > right.offset:
            raise ValueError(f"partition overlap: {left.name}/{right.name}")
    if ordered[-1].offset + ordered[-1].size > FLASH_SIZE:
        raise ValueError("partition table exceeds 32MB")

    expected = {
        "nvs": ("data", "nvs", 0x9000, 0x5000),
        "otadata": ("data", "ota", 0xE000, 0x2000),
        "app0": ("app", "ota_0", 0x10000, OTA_SLOT_SIZE),
        "app1": ("app", "ota_1", 0xB10000, OTA_SLOT_SIZE),
        "littlefs": ("data", "spiffs", 0x1610000, 0x9F0000),
    }
    for name, values in expected.items():
        row = by_name[name]
        actual = (row.type, row.subtype, row.offset, row.size)
        if actual != values:
            if name in {"app0", "app1"} and row.size != OTA_SLOT_SIZE:
                raise ValueError("OTA slots must both be 11MB")
            raise ValueError(f"unexpected partition definition: {name}")


def compare_layouts(
    expected: Sequence[Partition], actual: Sequence[Partition]
) -> None:
    def normalized(rows: Sequence[Partition]):
        return sorted(
            (
                row.name,
                row.type,
                row.subtype,
                row.offset,
                row.size,
                row.flags,
            )
            for row in rows
        )

    if normalized(expected) != normalized(actual):
        raise ValueError("compiled partition table differs from source CSV")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", required=True, type=Path)
    parser.add_argument("--binary", type=Path)
    args = parser.parse_args()

    source = parse_csv(args.csv)
    validate_firefly_layout(source)
    if args.binary is not None:
        compiled = parse_binary(args.binary)
        validate_firefly_layout(compiled)
        compare_layouts(source, compiled)
    print(
        f"validated {len(source)} partitions; "
        f"OTA slots={OTA_SLOT_SIZE:#x}; flash={FLASH_SIZE:#x}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
