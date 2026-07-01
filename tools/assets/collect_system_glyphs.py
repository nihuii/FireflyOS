"""Collect the minimal CJK glyph set used by FireflyOS source files."""

from argparse import ArgumentParser
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".ino"}


def is_cjk(character: str) -> bool:
    codepoint = ord(character)
    return (
        0x3400 <= codepoint <= 0x4DBF
        or 0x4E00 <= codepoint <= 0x9FFF
        or 0xF900 <= codepoint <= 0xFAFF
    )


def collect_glyphs(source_root, seed: str = "") -> str:
    ordered = []
    seen = set()

    def add_text(text: str) -> None:
        for character in text:
            if is_cjk(character) and character not in seen:
                seen.add(character)
                ordered.append(character)

    add_text(seed)
    roots = [source_root] if isinstance(source_root, Path) else list(source_root)
    for root in roots:
        for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            add_text(path.read_text(encoding="utf-8", errors="ignore"))
    return "".join(ordered)


def write_glyphs(source_root: Path, output: Path, seed: str = "") -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(collect_glyphs(source_root, seed) + "\n", encoding="utf-8")


def main() -> None:
    parser = ArgumentParser()
    parser.add_argument("--root", type=Path, nargs="+", required=True)
    parser.add_argument("--seed", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    seed = args.seed.read_text(encoding="utf-8") if args.seed else ""
    write_glyphs(args.root, args.output, seed)


if __name__ == "__main__":
    main()
