#!/usr/bin/env python3
"""Structural verification for generated FireflyOS UI previews."""

from __future__ import annotations

import ast
import json
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "image" / "ui_mockups"


def verify_group(folder: str, expected_count: int, expected_size: tuple[int, int]):
    paths = sorted((OUT / folder).glob("*.png"))
    assert len(paths) == expected_count, f"{folder}: expected {expected_count}, got {len(paths)}"
    for path in paths:
        with Image.open(path) as im:
            assert im.size == expected_size, f"{path.name}: {im.size} != {expected_size}"
            assert im.mode == "RGBA", f"{path.name}: expected RGBA, got {im.mode}"
            assert im.getpixel((0, 0))[3] == 0, f"{path.name}: top-left corner is not clipped"
            assert im.getpixel((im.width // 2, im.height // 2))[3] == 255, f"{path.name}: center is transparent"
            extrema = im.convert("RGB").getextrema()
            assert any(lo != hi for lo, hi in extrema), f"{path.name}: image is visually empty"
    return paths


def main():
    watch = verify_group("watch", 66, (410, 502))
    android = verify_group("android", 18, (432, 960))

    manifest = json.loads((OUT / "manifest.json").read_text(encoding="utf-8"))
    assert len(manifest) == 84, f"manifest: expected 84 entries, got {len(manifest)}"
    manifest_files = {item["path"] for item in manifest}
    disk_files = {f"watch/{p.name}" for p in watch} | {f"android/{p.name}" for p in android}
    assert manifest_files == disk_files, "manifest and generated PNG names differ"

    assert len(list(OUT.glob("watch-contact-sheet-*.png"))) == 5
    assert len(list(OUT.glob("android-contact-sheet-*.png"))) == 3
    for required in (OUT / "README.md", OUT / "index.html", OUT / "guides" / "watch-safe-area.png"):
        assert required.is_file() and required.stat().st_size > 0, f"missing {required}"

    source = (ROOT / "tools" / "render_ui_mockups.py").read_text(encoding="utf-8")
    ast.parse(source)
    print("PASS: 66 watch + 18 Android screens; dimensions, RGBA corners, manifest, guides and galleries verified")


if __name__ == "__main__":
    main()
