from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
HTML_PATH = ROOT / "docs" / "UI预览" / "01-Shell" / "index.html"


def validate_preview(html: str) -> list[str]:
    errors: list[str] = []
    required = [
        "glance-screen",
        "lock-screen",
        "home-screen",
        "control-center",
        "notification-center",
        "app-shell",
    ]
    missing = [name for name in required if f'id="{name}"' not in html]
    if missing:
        errors.append(f"missing preview frames: {missing}")
    if len(re.findall(r'class="watch-frame"', html)) < 6:
        errors.append("at least six 410x502 frames are required")
    if html.count('class="safe-area"') < 6:
        errors.append("every preview frame needs a safe-area overlay")
    if "width:410px" not in html or "height:502px" not in html:
        errors.append("watch frames must declare exact 410x502 dimensions")
    return errors


def main() -> int:
    if not HTML_PATH.is_file():
        raise SystemExit(f"preview HTML not found: {HTML_PATH}")
    errors = validate_preview(HTML_PATH.read_text(encoding="utf-8"))
    if errors:
        raise SystemExit("; ".join(errors))
    print("UI preview contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
