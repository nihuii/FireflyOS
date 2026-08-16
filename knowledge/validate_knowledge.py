#!/usr/bin/env python3
"""Validate the lightweight FireflyOS design-reference knowledge base."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote


KNOWLEDGE_DIR = Path(__file__).resolve().parent
REPO_ROOT = KNOWLEDGE_DIR.parent
PLAN_DIR = REPO_ROOT / "docs" / "执行计划"
CURRENT_BASELINE = "fab5509"
CURRENT_BASELINE_FULL = "fab5509b94f17b37d9d10233d10660c979ee3507"
LEGACY_COMMIT = "e4fb0d1"

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

PLAN_FILES = [
    "00-总体执行路线图.md",
    "01-工程基线与模块化核心.md",
    "01A-计划1缺口补全.md",
    "02-UI-Shell与视觉系统.md",
    "02A-UI-Shell缺口补全.md",
    "03-核心应用电源与IMU.md",
    "04-存储音频与媒体应用.md",
    "05-BLE与Android伴侣应用.md",
    "06-WiFi天气OTA与发布验收.md",
]

STANDARD_PLAN_FILES = PLAN_FILES[1:]
STANDARD_PLAN_HEADINGS = [
    "## 1. 目标与当前状态",
    "## 2. 知识库依据",
    "## 3. 前置条件与禁止事项",
    "## 4. 功能范围与明确非目标",
    "## 5. 分任务实施顺序",
    "## 6. 自动验证",
    "## 7. 真机验收",
    "## 8. 退出门槛与下一计划",
]

PRIMARY_PLAN_BY_CARD = {
    "01": "计划 1",
    "02": "计划 2",
    "03": "计划 3",
    "04": "计划 3",
    "05": "计划 4",
    "06": "计划 4",
    "07": "计划 2",
    "08": "计划 5",
    "09": "计划 5",
    "10": "计划 6",
    "11": "计划 6",
    "12": "计划 6",
    "13": "计划 1",
}

REQUIRED_CARD_HEADINGS = [
    "## 目标",
    "## 推荐边界",
    "## 最小实现",
    "## 主要风险",
    "## 参考路径",
]

MARKDOWN_LINK = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
LEVEL_TWO_HEADING = re.compile(r"^## .+$", re.MULTILINE)
TASK_HEADING = re.compile(r"^### (Task[^\r\n]+)$", re.MULTILINE)
MAIN_SHA_CLAIM = re.compile(
    r"\bmain\b[^\r\n]{0,24}?\b([0-9a-f]{7,40})\b", re.IGNORECASE
)
AHEAD_COUNT_CLAIM = re.compile(r"\bahead\s+\d+\b", re.IGNORECASE)
PLACEHOLDER = re.compile(r"\b(?:TBD|TODO|FIXME)\b", re.IGNORECASE)
PRIVATE_KEY = re.compile(r"-----BEGIN (?:EC |RSA |OPENSSH )?PRIVATE KEY-----")
ABSOLUTE_WINDOWS_PATH = re.compile(r"[A-Za-z]:[\\/]")
OBSOLETE_REMOTE_CLAIM = re.compile(r"已推送[^\n]*codex|codex[^\n]*远端[^\n]*取回")
REQUIRED_TASK_MARKERS = ("文件范围", "预期失败", "最小实现", "验证命令", "预期结果")
PLAN_FORBIDDEN = {
    "removed worktree path": re.compile(r"\.worktrees[\\/]", re.IGNORECASE),
    "removed feature branch": re.compile(r"(?:origin/)?codex/(?:firefly-core|ui-shell|core-apps-power-imu|storage-audio-media|ble-android|wifi-weather-ota)"),
    "Git mutation command": re.compile(r"\bgit\s+(?:add|commit|merge|push|tag|checkout|switch)\b", re.IGNORECASE),
    "deleted project introduction": re.compile(r"docs/项目介绍\.md"),
    "obsolete LittleFS implementation": re.compile(r"(?:启用|使用|写入|缓存到|分区[^\n]{0,20})\s*`?LittleFS|littlefs\s*,", re.IGNORECASE),
}


def configure_console() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure:
            reconfigure(encoding="utf-8")


def read_text(name: str, errors: list[str]) -> str:
    path = KNOWLEDGE_DIR / name
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        errors.append(f"{name}: cannot read as UTF-8: {exc}")
        return ""


def read_plan_text(name: str, errors: list[str]) -> str:
    path = PLAN_DIR / name
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        errors.append(f"docs/执行计划/{name}: cannot read as UTF-8: {exc}")
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
        if trailing:
            errors.append(f"{name}:{line_number}: trailing whitespace")


def check_cards(texts: dict[str, str], errors: list[str]) -> None:
    for name in MODULE_FILES:
        text = texts.get(name, "")
        if not text.startswith("# "):
            errors.append(f"{name}: missing level-one title")
        actual_headings = LEVEL_TWO_HEADING.findall(text)
        if actual_headings != REQUIRED_CARD_HEADINGS:
            errors.append(
                f"{name}: headings must be exactly "
                + ", ".join(REQUIRED_CARD_HEADINGS)
            )


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


def check_location_rules(texts: dict[str, str], errors: list[str]) -> None:
    for name, text in texts.items():
        normalized = text.replace("\\", "/")
        if ".worktrees/" in normalized:
            errors.append(f"{name}: contains a removed worktree path")
        if "origin/codex/" in normalized:
            errors.append(f"{name}: contains a deleted remote branch path")
        if ABSOLUTE_WINDOWS_PATH.search(text):
            errors.append(f"{name}: contains a machine-specific absolute path")
        if OBSOLETE_REMOTE_CLAIM.search(text):
            errors.append(f"{name}: contains an obsolete remote recovery claim")


def git_object_exists(spec: str) -> bool:
    result = subprocess.run(
        ["git", "cat-file", "-e", spec],
        cwd=REPO_ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.returncode == 0


def check_baseline(texts: dict[str, str], errors: list[str]) -> None:
    if not git_object_exists(f"{CURRENT_BASELINE}^{{commit}}"):
        errors.append(f"current baseline commit is unavailable: {CURRENT_BASELINE}")
    expected_label = f"main@{CURRENT_BASELINE}"
    for name in ("README.md", "00-源码清单与取回方法.md"):
        if expected_label not in texts.get(name, ""):
            errors.append(f"{name}: missing current baseline {expected_label}")


def check_plan_links(plan_texts: dict[str, str], errors: list[str]) -> None:
    root = REPO_ROOT.resolve()
    for name, text in plan_texts.items():
        source = PLAN_DIR / name
        for raw_target in MARKDOWN_LINK.findall(text):
            target = raw_target.strip().strip("<>")
            if target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            target = unquote(target.split("#", 1)[0])
            if not target:
                continue
            resolved = (source.parent / target).resolve()
            if resolved != root and root not in resolved.parents:
                errors.append(
                    f"docs/执行计划/{name}: link escapes repository: {raw_target}"
                )
            elif not resolved.is_file():
                errors.append(
                    f"docs/执行计划/{name}: broken internal link: {raw_target}"
                )


def check_execution_plans(errors: list[str]) -> None:
    actual = {path.name for path in PLAN_DIR.glob("*.md")}
    for name in sorted(set(PLAN_FILES) - actual):
        errors.append(f"missing execution plan: {name}")

    texts = {
        name: read_plan_text(name, errors)
        for name in PLAN_FILES
        if (PLAN_DIR / name).is_file()
    }
    for name, text in texts.items():
        display_name = f"docs/执行计划/{name}"
        check_text_hygiene(display_name, text, errors)
        for label, pattern in PLAN_FORBIDDEN.items():
            if pattern.search(text.replace("\\", "/")):
                errors.append(f"{display_name}: contains {label}")
        for match in MAIN_SHA_CLAIM.finditer(text):
            claimed_sha = match.group(1).lower()
            if claimed_sha not in (CURRENT_BASELINE, CURRENT_BASELINE_FULL):
                errors.append(
                    f"{display_name}: contains stale main SHA {claimed_sha}"
                )
        for match in AHEAD_COUNT_CLAIM.finditer(text):
            errors.append(
                f"{display_name}: contains stale ahead count {match.group(0)}"
            )

    for name in STANDARD_PLAN_FILES:
        text = texts.get(name, "")
        headings = LEVEL_TWO_HEADING.findall(text)
        if headings != STANDARD_PLAN_HEADINGS:
            errors.append(
                f"docs/执行计划/{name}: headings must be exactly "
                + ", ".join(STANDARD_PLAN_HEADINGS)
            )
        task_matches = list(TASK_HEADING.finditer(text))
        if not task_matches:
            errors.append(f"docs/执行计划/{name}: missing Task sections")
        for index, task_match in enumerate(task_matches):
            section_end_candidates = [len(text)]
            if index + 1 < len(task_matches):
                section_end_candidates.append(task_matches[index + 1].start())
            next_level_two = LEVEL_TWO_HEADING.search(text, task_match.end())
            if next_level_two:
                section_end_candidates.append(next_level_two.start())
            section_end = min(section_end_candidates)
            section = text[task_match.end() : section_end]
            task_name = task_match.group(1)
            if "- [ ]" not in section:
                errors.append(
                    f"docs/执行计划/{name}: {task_name} missing task checkbox"
                )
            for marker in REQUIRED_TASK_MARKERS:
                if f"**{marker}：**" not in section:
                    errors.append(
                        f"docs/执行计划/{name}: {task_name} missing task marker {marker}"
                    )

    roadmap = texts.get("00-总体执行路线图.md", "")
    if not any(
        f"main@{sha}" in roadmap
        for sha in (CURRENT_BASELINE, CURRENT_BASELINE_FULL)
    ):
        errors.append("docs/执行计划/00-总体执行路线图.md: missing current baseline")
    for state in ("当前已有", "部分已有", "待开发", "待真机", "阻塞"):
        if state not in roadmap:
            errors.append(
                f"docs/执行计划/00-总体执行路线图.md: missing state {state}"
            )

    mapping_heading = "## 7. 知识库唯一主要计划映射"
    mapping_start = roadmap.find(mapping_heading)
    mapping_text = roadmap[mapping_start:] if mapping_start >= 0 else ""
    rows = re.findall(
        r"^\|\s*(0[1-9]|1[0-3])\s*\|\s*(计划\s+[1-6])\s*\|",
        mapping_text,
        re.MULTILINE,
    )
    found: dict[str, list[str]] = {}
    for card, plan in rows:
        found.setdefault(card, []).append(plan)
    for card, expected_plan in PRIMARY_PLAN_BY_CARD.items():
        plans = found.get(card, [])
        if plans != [expected_plan]:
            errors.append(
                "docs/执行计划/00-总体执行路线图.md: "
                f"card {card} must map exactly once to {expected_plan}, got {plans}"
            )

    check_plan_links(texts, errors)


def main() -> int:
    configure_console()
    errors: list[str] = []
    check_file_set(errors)

    texts = {
        name: read_text(name, errors)
        for name in EXPECTED_MARKDOWN_FILES
        if (KNOWLEDGE_DIR / name).is_file()
    }
    for name, text in texts.items():
        check_text_hygiene(name, text, errors)

    check_cards(texts, errors)
    check_links(texts, errors)
    check_location_rules(texts, errors)
    check_baseline(texts, errors)
    check_execution_plans(errors)

    if errors:
        for error in errors:
            print(f"[FAIL] {error}", file=sys.stderr)
        print(f"Knowledge validation failed: {len(errors)} issue(s).", file=sys.stderr)
        return 1

    if git_object_exists(f"{LEGACY_COMMIT}^{{commit}}"):
        print(f"[INFO] Optional local legacy object is available: {LEGACY_COMMIT}")
    else:
        print(
            f"[WARN] Optional local legacy object is unavailable: {LEGACY_COMMIT}. "
            "Module cards remain self-contained."
        )

    print(
        "Knowledge validation passed: "
        f"{len(EXPECTED_MARKDOWN_FILES)} Markdown files, "
        f"{len(MODULE_FILES)} module/integration cards, "
        f"{len(PLAN_FILES)} aligned execution plans."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
