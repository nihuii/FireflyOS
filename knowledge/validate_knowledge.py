#!/usr/bin/env python3
"""Validate the curated FireflyOS legacy implementation knowledge base."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote


KNOWLEDGE_DIR = Path(__file__).resolve().parent
REPO_ROOT = KNOWLEDGE_DIR.parent
SOURCE_COMMIT = "e4fb0d1"
FULL_SOURCE_COMMIT = "e4fb0d1ff71bcc0330b507fa90c653be9929e611"
BASELINE_COMMIT = "351dff7"

MODULE_FILES = [
    "01-核心运行时与事件状态.md",
    "02-UI-Shell与导航覆盖层.md",
    "03-时间闹钟与调度.md",
    "04-电源按键触摸与IMU.md",
    "05-存储文件与SD卡.md",
    "06-音频音乐与录音.md",
    "07-主题包与资源管理.md",
    "08-BLE协议配对与通知.md",
    "09-Android伴侣应用.md",
    "10-WiFi-NTP与天气.md",
    "11-大文件传输.md",
    "12-OTA更新与首启回滚.md",
    "13-诊断恢复出厂与发布闭锁.md",
    "14-主程序集成与后续复用顺序.md",
]

EXPECTED_MARKDOWN_FILES = [
    "README.md",
    "00-源码清单与取回方法.md",
    *MODULE_FILES,
    "知识库设计.md",
    "知识库实施计划.md",
]

PUBLISHED_FILES = [
    "README.md",
    "00-源码清单与取回方法.md",
    *MODULE_FILES,
]

REQUIRED_MODULE_HEADINGS = [
    "## 来源",
    "## 复用等级",
    "## 模块定位",
    "## 职责与边界",
    "## 数据流与线程边界",
    "## 关键接口",
    "## 精选代码",
    "## 源码与测试映射",
    "## 验证边界",
    "## 已知问题",
    "## 基于 main 的复用步骤",
]

MARKDOWN_LINK = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
GIT_SHOW_SOURCE = re.compile(
    rf"git show\s+[\"`]?{SOURCE_COMMIT}:([^\"`\r\n]+)"
)
PLACEHOLDER = re.compile(r"\b(?:TBD|TODO|FIXME)\b", re.IGNORECASE)
PRIVATE_KEY = re.compile(
    r"-----BEGIN (?:EC |RSA |OPENSSH )?PRIVATE KEY-----"
)
ABSOLUTE_WINDOWS_PATH = re.compile(r"[A-Za-z]:\\")


def read_text(name: str, errors: list[str]) -> str:
    path = KNOWLEDGE_DIR / name
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        errors.append(f"{name}: cannot read as UTF-8: {exc}")
        return ""


def check_file_set(errors: list[str]) -> None:
    actual = {path.name for path in KNOWLEDGE_DIR.glob("*.md")}
    expected = set(EXPECTED_MARKDOWN_FILES)
    for name in sorted(expected - actual):
        errors.append(f"missing Markdown file: {name}")
    for name in sorted(actual - expected):
        errors.append(f"unexpected Markdown file: {name}")


def check_text_hygiene(name: str, text: str, errors: list[str]) -> None:
    if "\x00" in text:
        errors.append(f"{name}: contains a NUL byte")
    if PLACEHOLDER.search(text):
        errors.append(f"{name}: contains a placeholder token")
    if PRIVATE_KEY.search(text):
        errors.append(f"{name}: contains private-key material")
    for line_number, line in enumerate(text.splitlines(), start=1):
        trailing = line[len(line.rstrip()) :]
        if trailing and trailing != "  ":
            errors.append(f"{name}:{line_number}: trailing whitespace")


def check_modules(texts: dict[str, str], errors: list[str]) -> None:
    for name in MODULE_FILES:
        text = texts.get(name, "")
        if not text.startswith("# "):
            errors.append(f"{name}: missing level-one title")
        for heading in REQUIRED_MODULE_HEADINGS:
            if heading not in text:
                errors.append(f"{name}: missing heading {heading}")
        if FULL_SOURCE_COMMIT not in text:
            errors.append(f"{name}: missing full source commit")
        if "PENDING" not in text:
            errors.append(f"{name}: missing formal hardware PENDING boundary")
        if not GIT_SHOW_SOURCE.search(text):
            errors.append(f"{name}: missing a git show retrieval command")


def check_links(texts: dict[str, str], errors: list[str]) -> None:
    root = KNOWLEDGE_DIR.resolve()
    for name, text in texts.items():
        source = KNOWLEDGE_DIR / name
        for raw_target in MARKDOWN_LINK.findall(text):
            target = raw_target.strip().strip("<>")
            if target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            target = unquote(target.split("#", 1)[0])
            if not target:
                continue
            resolved = (source.parent / target).resolve()
            if resolved != root and root not in resolved.parents:
                errors.append(f"{name}: link escapes knowledge directory: {raw_target}")
            elif not resolved.is_file():
                errors.append(f"{name}: broken internal link: {raw_target}")


def check_published_paths(texts: dict[str, str], errors: list[str]) -> None:
    for name in PUBLISHED_FILES:
        text = texts.get(name, "")
        normalized = text.replace("\\", "/")
        if ".worktrees/wifi-weather-ota" in normalized:
            errors.append(f"{name}: depends on a disposable worktree path")
        if ABSOLUTE_WINDOWS_PATH.search(text):
            errors.append(f"{name}: contains a machine-specific absolute path")


def check_git_sources(texts: dict[str, str], errors: list[str]) -> None:
    sources: set[str] = set()
    for name in ["00-源码清单与取回方法.md", *MODULE_FILES]:
        for match in GIT_SHOW_SOURCE.finditer(texts.get(name, "")):
            path = match.group(1).strip()
            if path:
                sources.add(path)

    if len(sources) < len(MODULE_FILES):
        errors.append(
            f"only {len(sources)} distinct git show source paths found for "
            f"{len(MODULE_FILES)} modules"
        )

    for path in sorted(sources):
        result = subprocess.run(
            ["git", "cat-file", "-e", f"{SOURCE_COMMIT}:{path}"],
            cwd=REPO_ROOT,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        if result.returncode != 0:
            detail = result.stderr.strip() or "Git object not found"
            errors.append(f"invalid Git source {SOURCE_COMMIT}:{path}: {detail}")

    baseline = subprocess.run(
        ["git", "cat-file", "-e", f"{BASELINE_COMMIT}^{{commit}}"],
        cwd=REPO_ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if baseline.returncode != 0:
        errors.append(f"invalid restart baseline commit: {BASELINE_COMMIT}")

    for name in ("README.md", "00-源码清单与取回方法.md"):
        if BASELINE_COMMIT not in texts.get(name, ""):
            errors.append(f"{name}: missing restart baseline {BASELINE_COMMIT}")


def main() -> int:
    errors: list[str] = []
    check_file_set(errors)

    texts = {
        name: read_text(name, errors)
        for name in EXPECTED_MARKDOWN_FILES
        if (KNOWLEDGE_DIR / name).is_file()
    }
    for name, text in texts.items():
        check_text_hygiene(name, text, errors)

    check_modules(texts, errors)
    check_links(texts, errors)
    check_published_paths(texts, errors)
    check_git_sources(texts, errors)

    if errors:
        for error in errors:
            print(f"[FAIL] {error}", file=sys.stderr)
        print(f"Knowledge validation failed: {len(errors)} issue(s).", file=sys.stderr)
        return 1

    print(
        "Knowledge validation passed: "
        f"{len(EXPECTED_MARKDOWN_FILES)} Markdown files, "
        f"{len(MODULE_FILES)} modules."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
