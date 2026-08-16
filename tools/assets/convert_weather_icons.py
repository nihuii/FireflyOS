"""Convert the approved 4x3 weather icon sheet into LVGL 8 A8 assets."""

from __future__ import annotations

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "docs" / "UI预览" / "05-天气与更新" / "天气图标母图-v1.png"
OUTPUT_DIR = ROOT / "libraries" / "FireflyOS" / "src" / "firefly" / "apps" / "weather"
HEADER = OUTPUT_DIR / "WeatherIcons.h"
SOURCE_CPP = OUTPUT_DIR / "WeatherIcons.cpp"
ICON_SIZE = 48
GRID_COLUMNS = 4
GRID_ROWS = 3
ICON_NAMES = (
    "clear_day",
    "clear_night",
    "partly_cloudy",
    "cloudy",
    "showers",
    "heavy_rain",
    "thunderstorm",
    "snow",
    "fog",
    "wind",
    "light_rain",
    "sleet",
)


def format_bytes(values: bytes) -> str:
    rows = []
    for offset in range(0, len(values), 16):
        row = values[offset : offset + 16]
        rows.append("  " + ", ".join(f"0x{value:02X}" for value in row) + ",")
    return "\n".join(rows)


def load_icons() -> list[bytes]:
    with Image.open(SOURCE) as original:
        image = original.convert("L")
        width, height = image.size
        icons = []
        for row in range(GRID_ROWS):
            for column in range(GRID_COLUMNS):
                left = round(column * width / GRID_COLUMNS)
                right = round((column + 1) * width / GRID_COLUMNS)
                top = round(row * height / GRID_ROWS)
                bottom = round((row + 1) * height / GRID_ROWS)
                cell = image.crop(
                    (left, top, right, bottom)
                )
                foreground = cell.point(lambda value: 255 if value > 10 else 0)
                bounds = foreground.getbbox()
                if bounds is None:
                    raise ValueError(f"weather icon cell {row},{column} is empty")
                content = cell.crop(bounds)
                side = max(content.size)
                margin = max(2, round(side * 0.08))
                square = Image.new("L", (side + 2 * margin, side + 2 * margin), 0)
                square.paste(
                    content,
                    (
                        (square.size[0] - content.size[0]) // 2,
                        (square.size[1] - content.size[1]) // 2,
                    ),
                )
                resized = square.resize(
                    (ICON_SIZE, ICON_SIZE),
                    resample=Image.Resampling.LANCZOS,
                )
                icons.append(resized.tobytes())
        return icons


def render_header() -> str:
    declarations = "\n".join(
        f"extern const lv_img_dsc_t weather_icon_{name};" for name in ICON_NAMES
    )
    return f"""#pragma once

#include <lvgl.h>

namespace firefly {{

{declarations}

}}  // namespace firefly
"""


def render_source(icons: list[bytes]) -> str:
    sections = ['#include "WeatherIcons.h"', "", "namespace firefly {", ""]
    for name, values in zip(ICON_NAMES, icons, strict=True):
        sections.extend(
            (
                f"const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t weather_icon_{name}_map[] = {{",
                format_bytes(values),
                "};",
                "",
                f"const lv_img_dsc_t weather_icon_{name} = {{",
                "  { LV_IMG_CF_ALPHA_8BIT, 0, 0, 48, 48 },",
                "  2304,",
                f"  weather_icon_{name}_map",
                "};",
                "",
            )
        )
    sections.extend(("}  // namespace firefly", ""))
    return "\n".join(sections)


def main() -> None:
    icons = load_icons()
    if len(icons) != len(ICON_NAMES) or any(
        len(icon) != ICON_SIZE * ICON_SIZE for icon in icons
    ):
        raise ValueError("weather icon conversion produced an invalid fixed-size resource")
    HEADER.write_text(render_header(), encoding="utf-8", newline="\n")
    SOURCE_CPP.write_text(render_source(icons), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
