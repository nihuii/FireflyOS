#!/usr/bin/env python3
"""Render the FireflyOS watch and Android companion UI preview set.

The renderer is deliberately deterministic and uses only vector-like primitives.
This keeps every preview editable without baking final copyrighted character art
or high-cost effects into the firmware design.
"""

from __future__ import annotations

import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "image" / "ui_mockups"
WATCH_OUT = OUT / "watch"
ANDROID_OUT = OUT / "android"
GUIDE_OUT = OUT / "guides"

WATCH_W, WATCH_H = 410, 502
ANDROID_W, ANDROID_H = 432, 960
DISPLAY_RADIUS = 44

FONT_REGULAR = Path(r"C:\Windows\Fonts\msyh.ttc")
FONT_BOLD = Path(r"C:\Windows\Fonts\msyhbd.ttc")
FONT_NUM = Path(r"C:\Windows\Fonts\bahnschrift.ttf")

C = {
    "bg": "#05090C",
    "black": "#000000",
    "surface": "#0C1820",
    "surface2": "#10252D",
    "line": "#24414A",
    "line2": "#315A60",
    "primary": "#5FE7C7",
    "secondary": "#6EC4D6",
    "starlight": "#D8F7EE",
    "violet": "#A89BCB",
    "muted": "#86A0A5",
    "dim": "#51666B",
    "energy": "#A8FF60",
    "orange": "#FF8A45",
    "danger": "#FF5A5F",
    "yellow": "#F7CE68",
    "white": "#F2FFFB",
}


def font(size: int, bold: bool = False, numeric: bool = False) -> ImageFont.FreeTypeFont:
    path = FONT_NUM if numeric else (FONT_BOLD if bold else FONT_REGULAR)
    return ImageFont.truetype(str(path), size=size)


def rounded_mask(size: tuple[int, int], radius: int) -> Image.Image:
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius, fill=255)
    return mask


def hex_rgba(value: str, alpha: int = 255) -> tuple[int, int, int, int]:
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4)) + (alpha,)


def tint(value: str, alpha: int, base: str = C["surface"]) -> tuple[int, int, int, int]:
    """Pre-blend a translucent UI tint so the final display mask cannot flatten it."""
    fg = hex_rgba(value)[:3]
    bg = hex_rgba(base)[:3]
    a = max(0, min(255, alpha)) / 255.0
    return tuple(round(bg[i] * (1-a) + fg[i] * a) for i in range(3)) + (255,)


def rr(draw: ImageDraw.ImageDraw, box, radius=18, fill=C["surface"], outline=None, width=1):
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)


def text(draw: ImageDraw.ImageDraw, xy, value, size=18, color=C["white"], bold=False,
         anchor="la", numeric=False, spacing=4):
    draw.text(xy, value, font=font(size, bold=bold, numeric=numeric), fill=color,
              anchor=anchor, spacing=spacing)


def fit_text(draw: ImageDraw.ImageDraw, value: str, max_width: int, size: int,
             bold: bool = False, numeric: bool = False) -> int:
    while size > 10:
        box = draw.textbbox((0, 0), value, font=font(size, bold=bold, numeric=numeric))
        if box[2] - box[0] <= max_width:
            return size
        size -= 1
    return size


def glow(layer: Image.Image, center, radius, color=C["primary"], alpha=75):
    halo = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    hd = ImageDraw.Draw(halo)
    x, y = center
    hd.ellipse((x - radius, y - radius, x + radius, y + radius), fill=hex_rgba(color, alpha))
    halo = halo.filter(ImageFilter.GaussianBlur(max(8, radius // 2)))
    layer.alpha_composite(halo)


def energy_wings(draw: ImageDraw.ImageDraw, center, scale=1.0, color=C["primary"], sam=False):
    cx, cy = center
    w = int(58 * scale)
    h = int(34 * scale)
    lc = C["energy"] if sam else color
    for sign in (-1, 1):
        pts = [
            (cx + sign * 9 * scale, cy),
            (cx + sign * 30 * scale, cy - h),
            (cx + sign * w, cy - h * 0.55),
            (cx + sign * 33 * scale, cy + h * 0.25),
            (cx + sign * 49 * scale, cy + h),
            (cx + sign * 15 * scale, cy + h * 0.45),
        ]
        draw.line(pts, fill=lc, width=max(2, int(2 * scale)), joint="curve")
    r = int(10 * scale)
    draw.polygon([(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)],
                 fill=lc)
    draw.polygon([(cx, cy - r // 2), (cx + r // 2, cy), (cx, cy + r // 2),
                  (cx - r // 2, cy)], fill=C["bg"])


def icon(draw: ImageDraw.ImageDraw, name: str, box, color=C["primary"], width=3):
    """Small geometric icon vocabulary that stays readable at firmware sizes."""
    x0, y0, x1, y1 = map(int, box)
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    w, h = x1 - x0, y1 - y0
    r = min(w, h) // 2
    if name in {"clock", "timer"}:
        draw.ellipse((cx-r, cy-r, cx+r, cy+r), outline=color, width=width)
        draw.line((cx, cy, cx, cy-r+5), fill=color, width=width)
        draw.line((cx, cy, cx+r-6, cy+3), fill=color, width=width)
        if name == "timer":
            draw.line((cx-7, y0, cx+7, y0), fill=color, width=width)
    elif name == "calendar":
        rr(draw, (x0, y0+3, x1, y1), 5, fill=None, outline=color, width=width)
        draw.line((x0, y0+12, x1, y0+12), fill=color, width=width)
        for dx in (-7, 7):
            draw.ellipse((cx+dx-2, cy+5-2, cx+dx+2, cy+5+2), fill=color)
    elif name == "settings":
        draw.ellipse((cx-r, cy-r, cx+r, cy+r), outline=color, width=width)
        draw.ellipse((cx-5, cy-5, cx+5, cy+5), outline=color, width=width)
        for a in range(0, 360, 45):
            dx, dy = math.cos(math.radians(a))*r, math.sin(math.radians(a))*r
            draw.line((cx+dx*.72, cy+dy*.72, cx+dx*1.18, cy+dy*1.18), fill=color, width=width)
    elif name in {"activity", "steps"}:
        draw.ellipse((cx-11, cy-16, cx-3, cy-5), fill=color)
        draw.ellipse((cx+3, cy+5, cx+11, cy+16), fill=color)
        draw.ellipse((cx-8, cy-4, cx, cy+7), outline=color, width=width)
        draw.ellipse((cx, cy-7, cx+8, cy+4), outline=color, width=width)
    elif name == "calculator":
        rr(draw, (x0+3, y0, x1-3, y1), 5, fill=None, outline=color, width=width)
        draw.line((x0+8, y0+10, x1-8, y0+10), fill=color, width=width)
        for yy in (cy+2, cy+10):
            for xx in (cx-7, cx+7):
                draw.ellipse((xx-2, yy-2, xx+2, yy+2), fill=color)
    elif name == "flashlight":
        draw.polygon([(cx-11, y0), (cx+11, y0), (cx+6, cy-2), (cx-6, cy-2)], outline=color)
        rr(draw, (cx-6, cy-2, cx+6, y1), 3, fill=None, outline=color, width=width)
    elif name in {"files", "folder"}:
        draw.polygon([(x0, y0+9), (cx-3, y0+9), (cx+2, y0+3), (x1, y0+3),
                      (x1, y1), (x0, y1)], outline=color)
        draw.line((x0+2, cy, x1-2, cy), fill=color, width=width)
    elif name == "music":
        draw.line((cx+6, y0, cx+6, y1-7), fill=color, width=width)
        draw.line((cx+6, y0, x1, y0+5), fill=color, width=width)
        draw.ellipse((cx-5, y1-12, cx+7, y1), fill=color)
    elif name == "recorder":
        rr(draw, (cx-7, y0, cx+7, cy+6), 7, fill=None, outline=color, width=width)
        draw.arc((cx-15, cy-3, cx+15, y1-2), 0, 180, fill=color, width=width)
        draw.line((cx, y1-2, cx, y1+5), fill=color, width=width)
    elif name == "theme":
        draw.ellipse((cx-r, cy-r, cx+r, cy+r), outline=color, width=width)
        for dx, dy, c in [(-7,-5,C["primary"]),(7,-5,C["violet"]),(-3,8,C["secondary"])]:
            draw.ellipse((cx+dx-4, cy+dy-4, cx+dx+4, cy+dy+4), fill=c)
    elif name == "weather":
        draw.ellipse((x0+2, y0+2, cx+4, cy+5), outline=C["yellow"], width=width)
        rr(draw, (cx-10, cy-3, x1, y1-2), 9, fill=color)
    elif name in {"wifi", "signal"}:
        # Keep all three arcs valid even when the icon is rendered in an 18 px list slot.
        pads = (0, max(3, r // 2), max(6, r - 1))
        for pad in pads:
            draw.arc((x0+pad, y0+pad, x1-pad, y1-pad), 205, 335, fill=color, width=width)
        draw.ellipse((cx-2, y1-4, cx+2, y1), fill=color)
    elif name in {"bluetooth", "ble"}:
        draw.line((cx, y0, cx, y1), fill=color, width=width)
        draw.line((cx, y0, cx+10, cy-7, cx-8, cy+7, cx+10, y1, cx, y1), fill=color, width=width)
    elif name == "battery":
        rr(draw, (x0, y0+4, x1-4, y1-4), 4, fill=None, outline=color, width=width)
        draw.rectangle((x1-4, cy-5, x1, cy+5), fill=color)
        draw.rectangle((x0+5, y0+9, cx+5, y1-9), fill=color)
    elif name in {"update", "download"}:
        draw.line((cx, y0, cx, cy+7), fill=color, width=width)
        draw.line((cx-8, cy, cx, cy+8, cx+8, cy), fill=color, width=width)
        draw.arc((x0, cy-4, x1, y1), 0, 180, fill=color, width=width)
    elif name == "warning":
        draw.polygon([(cx, y0), (x1, y1), (x0, y1)], outline=color)
        draw.line((cx, y0+9, cx, cy+7), fill=color, width=width)
        draw.ellipse((cx-2, y1-8, cx+2, y1-4), fill=color)
    elif name == "check":
        draw.ellipse((x0, y0, x1, y1), outline=color, width=width)
        draw.line((x0+7, cy, cx-2, y1-8, x1-6, y0+8), fill=color, width=width)
    elif name == "close":
        draw.ellipse((x0, y0, x1, y1), outline=color, width=width)
        draw.line((x0+8, y0+8, x1-8, y1-8), fill=color, width=width)
        draw.line((x1-8, y0+8, x0+8, y1-8), fill=color, width=width)
    elif name == "phone":
        rr(draw, (x0+6, y0, x1-6, y1), 5, fill=None, outline=color, width=width)
        draw.line((cx-5, y1-5, cx+5, y1-5), fill=color, width=width)
    elif name == "bell":
        draw.arc((x0+4, y0, x1-4, y1-5), 180, 360, fill=color, width=width)
        draw.line((x0+4, cy, x0+4, y1-7, x1-4, y1-7, x1-4, cy), fill=color, width=width)
        draw.ellipse((cx-3, y1-5, cx+3, y1+1), fill=color)
    elif name == "lock":
        draw.arc((cx-10, y0, cx+10, cy+5), 180, 360, fill=color, width=width)
        rr(draw, (x0+3, cy-1, x1-3, y1), 5, fill=None, outline=color, width=width)
    elif name == "play":
        draw.ellipse((x0, y0, x1, y1), outline=color, width=width)
        draw.polygon([(cx-5, cy-9), (cx+9, cy), (cx-5, cy+9)], fill=color)
    elif name == "pause":
        draw.ellipse((x0, y0, x1, y1), outline=color, width=width)
        draw.rectangle((cx-7, cy-8, cx-2, cy+8), fill=color)
        draw.rectangle((cx+3, cy-8, cx+8, cy+8), fill=color)
    elif name == "search":
        draw.ellipse((x0, y0, x1-8, y1-8), outline=color, width=width)
        draw.line((x1-11, y1-11, x1, y1), fill=color, width=width)
    elif name == "info":
        draw.ellipse((x0, y0, x1, y1), outline=color, width=width)
        text(draw, (cx, cy+1), "i", 20, color, True, "mm", numeric=True)
    else:
        draw.ellipse((cx-r, cy-r, cx+r, cy+r), outline=color, width=width)
        draw.ellipse((cx-3, cy-3, cx+3, cy+3), fill=color)


def progress_arc(draw, box, value, color=C["primary"], track=C["line"], width=8, start=-90):
    draw.arc(box, 0, 359, fill=track, width=width)
    draw.arc(box, start, start + int(360 * max(0, min(1, value))), fill=color, width=width)


class WatchCanvas:
    def __init__(self, title: str = "", subtitle: str = "", sam: bool = False,
                 status: bool = True, time: str = "09:41"):
        self.im = Image.new("RGBA", (WATCH_W, WATCH_H), hex_rgba(C["bg"]))
        self.d = ImageDraw.Draw(self.im)
        self.sam = sam
        glow(self.im, (320, 95), 70, C["energy"] if sam else C["primary"], 35)
        glow(self.im, (70, 420), 58, C["orange"] if sam else C["violet"], 22)
        self.d = ImageDraw.Draw(self.im)
        if status:
            self.status_bar(time)
        if title:
            text(self.d, (28, 64), title, 25, C["white"], True)
            if subtitle:
                text(self.d, (29, 95), subtitle, 13, C["muted"])

    def status_bar(self, value="09:41", battery=82, connected=True):
        text(self.d, (31, 28), value, 15, C["starlight"], True, "lm", numeric=True)
        if connected:
            icon(self.d, "ble", (297, 19, 311, 35), C["secondary"], 2)
        for i, h in enumerate((4, 7, 10, 13)):
            self.d.rounded_rectangle((321+i*5, 34-h, 324+i*5, 34), 1, fill=C["muted"])
        self.d.rounded_rectangle((352, 21, 377, 34), 4, outline=C["muted"], width=2)
        self.d.rectangle((378, 25, 380, 30), fill=C["muted"])
        fill = C["danger"] if battery < 15 else C["primary"]
        self.d.rounded_rectangle((355, 24, 355 + int(18*battery/100), 31), 2, fill=fill)

    def gesture(self):
        self.d.rounded_rectangle((164, 477, 246, 481), 2, fill=tint(C["starlight"], 150, C["bg"]))

    def save(self, path: Path):
        self.im.putalpha(rounded_mask(self.im.size, DISPLAY_RADIUS))
        self.im.save(path)


def card(c: WatchCanvas, box, title_value="", subtitle="", icon_name=None, accent=C["primary"],
         right="", selected=False):
    fill = C["surface2"] if selected else C["surface"]
    rr(c.d, box, 18, fill=fill, outline=accent if selected else C["line"], width=1)
    x0, y0, x1, y1 = box
    tx = x0 + 18
    if icon_name:
        rr(c.d, (x0+12, y0+12, x0+48, y0+48), 11, fill=tint(accent, 28), outline=C["line"])
        icon(c.d, icon_name, (x0+21, y0+20, x0+39, y0+40), accent, 2)
        tx = x0 + 59
    text(c.d, (tx, y0+18), title_value, 16, C["white"], True)
    if subtitle:
        text(c.d, (tx, y0+42), subtitle, 12, C["muted"])
    if right:
        size = fit_text(c.d, right, 86, 13)
        text(c.d, (x1-14, (y0+y1)//2), right, size, accent, False, "rm")


def pill(c: WatchCanvas, box, label, active=True, accent=C["primary"]):
    rr(c.d, box, 16, fill=tint(accent, 58) if active else C["surface"],
       outline=accent if active else C["line"])
    text(c.d, ((box[0]+box[2])//2, (box[1]+box[3])//2), label, 13,
         accent if active else C["muted"], True, "mm")


def app_tile(c: WatchCanvas, x: int, y: int, name: str, icon_name: str, accent=C["primary"]):
    rr(c.d, (x, y, x+82, y+82), 24, fill=C["surface"], outline=C["line"])
    glow(c.im, (x+41, y+40), 25, accent, 28)
    c.d = ImageDraw.Draw(c.im)
    icon(c.d, icon_name, (x+25, y+22, x+57, y+54), accent, 3)
    text(c.d, (x+41, y+100), name, 14, C["starlight"], True, "mm")


def render_watch_boot(spec):
    c = WatchCanvas(status=False)
    glow(c.im, (205, 230), 100, C["primary"], 70)
    c.d = ImageDraw.Draw(c.im)
    energy_wings(c.d, (205, 221), 1.2)
    text(c.d, (205, 292), "FireflyOS", 30, C["starlight"], True, "mm")
    text(c.d, (205, 326), "让微光成为日常", 14, C["muted"], False, "mm")
    c.d.rounded_rectangle((120, 380, 290, 384), 2, fill=C["line"])
    c.d.rounded_rectangle((120, 380, 242, 384), 2, fill=C["primary"])
    text(c.d, (205, 405), "正在点亮系统", 12, C["muted"], False, "mm")
    return c


def render_watch_glance(spec):
    c = WatchCanvas(status=False)
    text(c.d, (205, 120), spec.get("time", "09:41"), 78, C["starlight"], True, "mm", numeric=True)
    text(c.d, (205, 176), "6月30日  星期二", 16, C["muted"], False, "mm")
    glow(c.im, (205, 310), 64, C["primary"], 45)
    c.d = ImageDraw.Draw(c.im)
    energy_wings(c.d, (205, 302), .75)
    text(c.d, (205, 390), "24°C  晴间多云", 15, C["secondary"], False, "mm")
    text(c.d, (205, 430), "82%", 14, C["muted"], True, "mm", numeric=True)
    return c


def render_watch_lock(spec):
    c = WatchCanvas(status=False)
    text(c.d, (205, 90), "09:41", 70, C["starlight"], True, "mm", numeric=True)
    text(c.d, (205, 142), "6月30日  星期二", 15, C["muted"], False, "mm")
    rr(c.d, (28, 200, 382, 290), 22, fill=C["surface"], outline=C["line"])
    icon(c.d, "calendar", (50, 225, 80, 255), C["primary"], 2)
    text(c.d, (98, 222), "下一项", 12, C["muted"])
    text(c.d, (98, 247), "10:30  项目复盘", 17, C["white"], True)
    text(c.d, (98, 271), "还有 49 分钟", 12, C["secondary"])
    icon(c.d, "lock", (187, 351, 223, 390), C["primary"], 2)
    text(c.d, (205, 424), "向上滑动解锁", 13, C["muted"], False, "mm")
    c.d.rounded_rectangle((184, 454, 226, 458), 2, fill=C["primary"])
    return c


def render_watch_home(spec):
    page = spec.get("page", 1)
    c = WatchCanvas(title="应用", subtitle=f"第 {page} 页 · 独立运行", time="09:41")
    tiles = spec["tiles"]
    xs, ys = (44, 164, 284), (126, 282)
    for (name, ico, accent), x, y in zip(tiles, [x for y in ys for x in xs], [y for y in ys for x in xs]):
        app_tile(c, x, y, name, ico, accent)
    for i in range(2):
        c.d.ellipse((197+i*13, 453, 203+i*13, 459), fill=C["primary"] if i == page-1 else C["line2"])
    c.gesture()
    return c


def render_watch_control(spec):
    c = WatchCanvas(title="控制中心", subtitle="轻触切换 · 长按进入设置")
    controls = [
        ("亮度", "72%", C["yellow"], "theme", True), ("音量", "60%", C["secondary"], "music", True),
        ("蓝牙", "已连接", C["primary"], "ble", True), ("Wi-Fi", "已关闭", C["muted"], "wifi", False),
        ("省电", "自动", C["energy"], "battery", True), ("锁定", "立即", C["violet"], "lock", False),
    ]
    for i, (name, state, accent, ico, active) in enumerate(controls):
        col, row = i % 2, i // 2
        x, y = 28 + col*181, 126 + row*98
        rr(c.d, (x, y, x+169, y+84), 22, fill=tint(accent, 46) if active else C["surface"],
           outline=accent if active else C["line"])
        icon(c.d, ico, (x+18, y+18, x+46, y+46), accent, 2)
        text(c.d, (x+58, y+20), name, 15, C["white"], True)
        text(c.d, (x+58, y+48), state, 12, accent if active else C["muted"])
    c.gesture()
    return c


def render_watch_notifications(spec):
    empty = spec.get("empty", False)
    c = WatchCanvas(title="通知", subtitle="本地保存最近 20 条")
    if empty:
        icon(c.d, "bell", (176, 185, 234, 245), C["line2"], 3)
        text(c.d, (205, 284), "暂时没有通知", 19, C["starlight"], True, "mm")
        text(c.d, (205, 316), "新的消息会出现在这里", 13, C["muted"], False, "mm")
    else:
        rows = [
            ("日历", "项目复盘将在 49 分钟后开始", "09:41", C["primary"]),
            ("手机", "Android 伴侣已重新连接", "09:36", C["secondary"]),
            ("天气", "傍晚可能有短时阵雨", "08:20", C["violet"]),
            ("系统", "昨晚睡眠模式节电 8%", "07:45", C["energy"]),
        ]
        for i, (app, body, tm, accent) in enumerate(rows):
            y = 118 + i*82
            rr(c.d, (28, y, 382, y+70), 18, fill=C["surface"], outline=C["line"])
            c.d.ellipse((44, y+18, 76, y+50), fill=tint(accent, 50), outline=accent)
            text(c.d, (60, y+34), app[:1], 14, accent, True, "mm")
            text(c.d, (90, y+15), app, 13, accent, True)
            text(c.d, (90, y+39), body, fit_text(c.d, body, 220, 13), C["white"])
            text(c.d, (363, y+17), tm, 11, C["dim"], False, "ra", numeric=True)
    c.gesture()
    return c


def render_watch_notification_detail(spec):
    c = WatchCanvas(title="日历", subtitle="今天 09:41")
    rr(c.d, (28, 126, 382, 324), 24, fill=C["surface"], outline=C["line"])
    pill(c, (48, 146, 118, 177), "日程", True)
    text(c.d, (48, 209), "项目复盘", 24, C["white"], True)
    text(c.d, (48, 249), "10:30–11:15", 15, C["secondary"], True, numeric=True)
    text(c.d, (48, 284), "整理当前进度并确定下一阶段任务。", 13, C["muted"])
    pill(c, (28, 352, 197, 411), "稍后提醒", False)
    pill(c, (213, 352, 382, 411), "标为已读", True)
    c.gesture()
    return c


def render_watch_overlay(spec):
    mode = spec["mode"]
    sam = mode in {"alarm", "critical", "find", "lowmem", "hw"}
    c = WatchCanvas(status=False, sam=sam)
    data = {
        "power": ("电源菜单", "选择设备状态", "battery", C["primary"]),
        "charging": ("正在充电", "预计 38 分钟充满", "battery", C["primary"]),
        "alarm": ("起床闹钟", "07:30  ·  工作日", "bell", C["energy"]),
        "critical": ("电量仅剩 5%", "即将进入极限省电", "warning", C["danger"]),
        "pair": ("连接新手机？", "配对码  482 913", "phone", C["secondary"]),
        "pair_ok": ("连接完成", "Android 伴侣已受信任", "check", C["primary"]),
        "hw": ("部分硬件不可用", "SD 卡与音频服务已停用", "warning", C["orange"]),
        "motion": ("运动传感器不可用", "抬腕与活动统计已暂停", "activity", C["orange"]),
        "sd": ("未检测到 SD 卡", "音乐、录音与主题导入功能受限", "files", C["orange"]),
        "permission": ("允许访问麦克风？", "录音只保存在本机", "recorder", C["violet"]),
        "find": ("我在这里", "来自手机的查找请求", "bell", C["energy"]),
        "reset": ("恢复出厂设置？", "本机数据与配对信息将清除", "warning", C["danger"]),
        "lowmem": ("已释放运行内存", "暂停动效并关闭后台页面", "warning", C["orange"]),
    }
    title_v, sub, ico, accent = data[mode]
    glow(c.im, (205, 155), 92, accent, 55)
    c.d = ImageDraw.Draw(c.im)
    if sam:
        for i in range(3):
            c.d.line((28+i*10, 64, 105+i*7, 64), fill=accent, width=2)
            c.d.line((305-i*7, 64, 382-i*10, 64), fill=accent, width=2)
    rr(c.d, (36, 72, 374, 430), 30, fill=C["surface"], outline=accent, width=2)
    icon(c.d, ico, (174, 112, 236, 174), accent, 4)
    text(c.d, (205, 216), title_v, 25, C["white"], True, "mm")
    text(c.d, (205, 253), sub, 14, C["muted"], False, "mm")
    if mode == "charging":
        progress_arc(c.d, (141, 280, 269, 408), .82, accent, width=10)
        text(c.d, (205, 344), "82%", 28, C["starlight"], True, "mm", numeric=True)
    elif mode == "power":
        for i, label in enumerate(("关机", "重启", "紧急模式")):
            pill(c, (55+i*101, 306, 146+i*101, 358), label, i == 1, C["primary"] if i < 2 else C["orange"])
    elif mode == "alarm":
        pill(c, (58, 302, 352, 365), "停止闹钟", True, accent)
        text(c.d, (205, 397), "下滑稍后提醒 10 分钟", 12, C["muted"], False, "mm")
    elif mode in {"pair", "permission"}:
        pill(c, (58, 314, 190, 369), "取消", False, accent)
        pill(c, (220, 314, 352, 369), "允许" if mode == "permission" else "连接", True, accent)
    elif mode == "reset":
        pill(c, (58, 314, 190, 369), "取消", False, accent)
        pill(c, (220, 314, 352, 369), "清除数据", True, accent)
    elif mode not in {"charging"}:
        pill(c, (76, 314, 334, 369), "知道了" if mode not in {"critical", "find"} else ("进入省电" if mode == "critical" else "停止响铃"), True, accent)
    return c


def render_watch_clock_hub(spec):
    c = WatchCanvas(title="时钟", subtitle="本地 RTC · 无手机也可靠运行")
    text(c.d, (205, 177), "09:41", 58, C["starlight"], True, "mm", numeric=True)
    text(c.d, (205, 220), "上海  UTC+8", 13, C["muted"], False, "mm")
    items = [("闹钟", "bell", C["primary"]), ("计时器", "timer", C["secondary"]), ("秒表", "clock", C["violet"])]
    for i, (label, ico, accent) in enumerate(items):
        x = 28 + i*123
        rr(c.d, (x, 271, x+108, 370), 22, fill=C["surface"], outline=C["line"])
        icon(c.d, ico, (x+37, 291, x+71, 325), accent, 3)
        text(c.d, (x+54, 350), label, 14, C["white"], True, "mm")
    pill(c, (105, 407, 305, 453), "世界时钟 +", False)
    c.gesture()
    return c


def render_watch_list(spec):
    c = WatchCanvas(title=spec["title"], subtitle=spec.get("subtitle", ""))
    rows = spec.get("rows", [])
    start = spec.get("start", 120)
    h = spec.get("height", 66)
    gap = spec.get("gap", 8)
    for i, row in enumerate(rows[:5]):
        y = start + i*(h+gap)
        card(c, (28, y, 382, y+h), row[0], row[1] if len(row)>1 else "",
             row[2] if len(row)>2 else None, row[3] if len(row)>3 else C["primary"],
             row[4] if len(row)>4 else "", row[5] if len(row)>5 else False)
    if spec.get("fab"):
        c.d.ellipse((318, 402, 374, 458), fill=C["primary"])
        text(c.d, (346, 430), "+", 30, C["bg"], True, "mm", numeric=True)
    c.gesture()
    return c


def render_watch_alarm_editor(spec):
    c = WatchCanvas(title="编辑闹钟", subtitle="修改会立即保存到本机 RTC")
    text(c.d, (205, 166), "07:30", 54, C["starlight"], True, "mm", numeric=True)
    c.d.line((82, 192, 328, 192), fill=C["line"], width=1)
    text(c.d, (205, 220), "工作日", 14, C["primary"], True, "mm")
    card(c, (28, 255, 382, 319), "重复", "周一 至 周五", "calendar", C["primary"], "更改")
    card(c, (28, 329, 382, 393), "铃声", "微光唤醒", "music", C["violet"], "试听")
    pill(c, (28, 411, 382, 459), "保存闹钟", True)
    c.gesture()
    return c


def render_watch_keyboard(spec):
    c = WatchCanvas(title="闹钟名称", subtitle="最多 16 个字符")
    rr(c.d, (28, 115, 382, 169), 15, fill=C["surface"], outline=C["primary"])
    text(c.d, (46, 142), "晨间唤醒|", 17, C["white"], False, "lm")
    keys = [list("1234567890"), list("QWERTYUIOP"), list("ASDFGHJKL"), list("ZXCVBNM")]
    y0 = 195
    for r, row in enumerate(keys):
        kw = 31 if r < 2 else (34 if r == 2 else 38)
        total = len(row)*kw + (len(row)-1)*3
        x = (WATCH_W-total)//2
        for k in row:
            rr(c.d, (x, y0+r*51, x+kw, y0+r*51+43), 9, fill=C["surface2"], outline=C["line"])
            text(c.d, (x+kw//2, y0+r*51+22), k, 13, C["starlight"], True, "mm", numeric=True)
            x += kw+3
    pill(c, (43, 407, 126, 452), "取消", False)
    pill(c, (137, 407, 273, 452), "空格", False)
    pill(c, (284, 407, 367, 452), "完成", True)
    c.gesture()
    return c


def render_watch_timer(spec):
    running = spec.get("running", False)
    title_v = "计时中" if running else "计时器"
    c = WatchCanvas(title=title_v, subtitle="可在后台持续计时")
    value = .64 if running else 0
    progress_arc(c.d, (92, 126, 318, 352), value, C["primary"], width=12)
    display = "12:48" if running else "05:00"
    text(c.d, (205, 235), display, 50, C["starlight"], True, "mm", numeric=True)
    text(c.d, (205, 279), "剩余时间" if running else "时  :  分  :  秒", 13, C["muted"], False, "mm")
    if running:
        pill(c, (46, 390, 190, 450), "取消", False, C["danger"])
        pill(c, (220, 390, 364, 450), "暂停", True)
    else:
        pill(c, (76, 390, 334, 450), "开始", True)
    c.gesture()
    return c


def render_watch_stopwatch(spec):
    c = WatchCanvas(title="秒表", subtitle="精确到 1/100 秒")
    text(c.d, (205, 165), "02:14.36", 48, C["starlight"], True, "mm", numeric=True)
    rows = [("#03", "00:38.22"), ("#02", "00:45.91"), ("#01", "00:50.23")]
    for i, (n, t) in enumerate(rows):
        y = 222+i*48
        text(c.d, (58, y), n, 14, C["muted"], True, "lm", numeric=True)
        text(c.d, (205, y), t, 17, C["white"], False, "mm", numeric=True)
        if i == 0: text(c.d, (350, y), "最快", 12, C["primary"], True, "rm")
    pill(c, (46, 400, 190, 456), "计次", False)
    pill(c, (220, 400, 364, 456), "暂停", True)
    c.gesture()
    return c


def render_watch_calendar(spec):
    agenda = spec.get("agenda", False)
    c = WatchCanvas(title="日历", subtitle="2026 年 6 月 · 本地日程")
    if agenda:
        pill(c, (28, 116, 197, 154), "月视图", False)
        pill(c, (213, 116, 382, 154), "日程", True)
        rows = [
            ("09:00", "晨间计划", "已完成"), ("10:30", "项目复盘", "45 分钟"),
            ("14:00", "UI 评审", "线上"), ("20:30", "散步", "30 分钟"),
        ]
        for i, (tm, title_v, meta) in enumerate(rows):
            y = 174+i*67
            c.d.line((68, y+17, 68, y+63), fill=C["line"], width=2)
            c.d.ellipse((62, y+11, 74, y+23), fill=C["primary"] if i==1 else C["line2"])
            text(c.d, (88, y+9), tm, 13, C["secondary"], True, numeric=True)
            text(c.d, (150, y+9), title_v, 16, C["white"], True)
            text(c.d, (150, y+37), meta, 12, C["muted"])
    else:
        pill(c, (28, 116, 197, 154), "月视图", True)
        pill(c, (213, 116, 382, 154), "日程", False)
        weekdays = "一二三四五六日"
        for i, ch in enumerate(weekdays):
            text(c.d, (55+i*50, 185), ch, 12, C["muted"], True, "mm")
        days = list(range(1, 31))
        for idx, day in enumerate(days):
            row, col = (idx+0)//7, (idx+0)%7
            x, y = 55+col*50, 221+row*48
            if day == 30:
                c.d.ellipse((x-18, y-18, x+18, y+18), fill=C["primary"])
            text(c.d, (x, y), str(day), 14, C["bg"] if day==30 else C["white"], day==30, "mm", numeric=True)
            if day in {3, 12, 18, 30}:
                c.d.ellipse((x-2, y+16, x+2, y+20), fill=C["violet"] if day != 30 else C["bg"])
    c.gesture()
    return c


def render_watch_activity(spec):
    detail = spec.get("detail", False)
    c = WatchCanvas(title="活动", subtitle="IMU 本地统计 · 今日")
    if detail:
        text(c.d, (38, 137), "每小时步数", 15, C["white"], True)
        values = [.1,.05,.07,.12,.25,.44,.67,.39,.55,.72,.48,.83]
        for i, v in enumerate(values):
            x = 40+i*28
            c.d.rounded_rectangle((x, 275-int(v*120), x+14, 275), 5, fill=C["primary"] if i>=8 else C["line2"])
        c.d.line((38, 284, 374, 284), fill=C["line"], width=1)
        text(c.d, (40, 303), "06", 11, C["dim"], numeric=True)
        text(c.d, (205, 303), "12", 11, C["dim"], False, "mm", numeric=True)
        text(c.d, (370, 303), "18", 11, C["dim"], False, "ra", numeric=True)
        card(c, (28, 342, 382, 408), "最长活跃时段", "17:00–18:00 · 1,126 步", "activity", C["primary"], "查看")
    else:
        progress_arc(c.d, (112, 120, 298, 306), .68, C["primary"], width=13)
        text(c.d, (205, 198), "6,842", 42, C["starlight"], True, "mm", numeric=True)
        text(c.d, (205, 237), "目标 10,000 步", 13, C["muted"], False, "mm")
        stats = [("4.8 km", "距离"), ("216", "千卡"), ("58 min", "活跃")]
        for i, (v, label) in enumerate(stats):
            x = 28+i*123
            rr(c.d, (x, 340, x+108, 414), 18, fill=C["surface"], outline=C["line"])
            text(c.d, (x+54, 363), v, 16, C["white"], True, "mm", numeric=True)
            text(c.d, (x+54, 393), label, 12, C["muted"], False, "mm")
    c.gesture()
    return c


def render_watch_calculator(spec):
    c = WatchCanvas(title="计算器", subtitle="本地轻量计算")
    text(c.d, (365, 128), "1,248 ÷ 6", 16, C["muted"], False, "ra", numeric=True)
    text(c.d, (365, 180), "208", 42, C["starlight"], True, "ra", numeric=True)
    labels = [["C","±","%","÷"],["7","8","9","×"],["4","5","6","−"],["1","2","3","+"],["0",".","⌫","="]]
    for r, row in enumerate(labels):
        for col, label in enumerate(row):
            x, y = 28+col*89, 218+r*49
            accent = C["primary"] if col==3 else (C["danger"] if label=="C" else C["white"])
            rr(c.d, (x, y, x+78, y+41), 13, fill=tint(accent, 46) if col==3 else C["surface"], outline=C["line"])
            text(c.d, (x+39, y+21), label, 18, accent, True, "mm", numeric=True)
    c.gesture()
    return c


def render_watch_flashlight(spec):
    c = WatchCanvas(status=False)
    c.d.rectangle((0, 0, WATCH_W, WATCH_H), fill="#E7FFF8")
    for i in range(4):
        c.d.rounded_rectangle((44, 30+i*4, 366, 34+i*4), 2, fill="#C5F5E7")
    rr(c.d, (72, 347, 338, 451), 28, fill="#092019", outline=C["primary"], width=2)
    icon(c.d, "flashlight", (111, 375, 151, 419), C["primary"], 3)
    text(c.d, (177, 382), "屏幕手电筒", 18, C["white"], True)
    text(c.d, (177, 414), "5 分钟后自动关闭", 12, C["muted"])
    return c


def render_watch_music(spec):
    mode = spec.get("mode", "library")
    c = WatchCanvas(title="音乐", subtitle="本地 SD 媒体库")
    if mode == "empty":
        icon(c.d, "music", (177, 180, 233, 242), C["line2"], 4)
        text(c.d, (205, 283), "还没有本地音乐", 19, C["white"], True, "mm")
        text(c.d, (205, 317), "插入 SD 卡或从手机传输", 13, C["muted"], False, "mm")
        pill(c, (86, 362, 324, 416), "打开文件传输", True)
    elif mode == "playing":
        rr(c.d, (112, 116, 298, 302), 34, fill=C["surface"], outline=C["line"])
        glow(c.im, (205, 207), 75, C["violet"], 40)
        c.d = ImageDraw.Draw(c.im)
        energy_wings(c.d, (205, 207), .72, C["violet"])
        text(c.d, (205, 331), "Fires of Firmament", 18, C["white"], True, "mm", numeric=True)
        text(c.d, (205, 361), "本地音乐 · FLAC", 12, C["muted"], False, "mm")
        c.d.rounded_rectangle((62, 390, 348, 395), 3, fill=C["line"])
        c.d.rounded_rectangle((62, 390, 214, 395), 3, fill=C["primary"])
        text(c.d, (62, 411), "02:14", 11, C["muted"], numeric=True)
        text(c.d, (348, 411), "04:02", 11, C["muted"], False, "ra", numeric=True)
        icon(c.d, "pause", (181, 425, 229, 473), C["primary"], 3)
    else:
        rows = [
            ("Fires of Firmament", "04:02 · FLAC", "music", C["primary"], ""),
            ("A Gentle Light", "03:36 · MP3", "music", C["violet"], ""),
            ("Across the Stars", "05:18 · FLAC", "music", C["secondary"], ""),
            ("Morning Signal", "02:48 · MP3", "music", C["energy"], ""),
        ]
        for i, row in enumerate(rows):
            card(c, (28, 118+i*76, 382, 184+i*76), *row)
        pill(c, (105, 430, 305, 463), "全部 18 首", False)
    c.gesture()
    return c


def render_watch_recorder(spec):
    mode = spec.get("mode", "idle")
    c = WatchCanvas(title="录音机", subtitle="音频仅保存在本机")
    if mode == "list":
        rows = [("项目想法 03", "今天 09:18 · 02:36", "recorder", C["violet"], "播放"),
                ("快速备忘", "昨天 21:42 · 00:48", "recorder", C["secondary"], "播放"),
                ("测试录音", "6月28日 · 00:12", "recorder", C["muted"], "播放")]
        for i, row in enumerate(rows): card(c, (28, 126+i*82, 382, 196+i*82), *row)
        pill(c, (86, 389, 324, 445), "新建录音", True, C["violet"])
    else:
        if mode == "recording":
            for i, h in enumerate([18,38,25,60,42,71,30,52,22,65,35,50,28,44,18]):
                x = 54+i*21
                c.d.rounded_rectangle((x, 245-h//2, x+7, 245+h//2), 3,
                                      fill=C["violet"] if i%3 else C["primary"])
            text(c.d, (205, 156), "00:02:36", 38, C["starlight"], True, "mm", numeric=True)
            text(c.d, (205, 323), "正在录音 · 12.4 MB", 13, C["danger"], True, "mm")
            pill(c, (86, 371, 324, 431), "停止并保存", True, C["danger"])
        else:
            icon(c.d, "recorder", (171, 142, 239, 218), C["violet"], 4)
            text(c.d, (205, 269), "准备录音", 22, C["white"], True, "mm")
            text(c.d, (205, 306), "可用空间约 6 小时", 13, C["muted"], False, "mm")
            pill(c, (86, 356, 324, 420), "开始录音", True, C["violet"])
    c.gesture()
    return c


def render_watch_themes(spec):
    mode = spec.get("mode", "gallery")
    c = WatchCanvas(title="主题", subtitle="轻量资源包 · 随时回退")
    if mode == "error":
        icon(c.d, "warning", (175, 150, 235, 210), C["orange"], 4)
        text(c.d, (205, 253), "主题包无法导入", 21, C["white"], True, "mm")
        text(c.d, (205, 289), "缺少 manifest.json 或资源过大", 13, C["muted"], False, "mm")
        pill(c, (70, 344, 340, 400), "查看导入规范", True, C["orange"])
    elif mode == "detail":
        rr(c.d, (68, 119, 342, 300), 28, fill=C["black"], outline=C["line"])
        glow(c.im, (205, 208), 70, C["primary"], 55)
        c.d = ImageDraw.Draw(c.im)
        energy_wings(c.d, (205, 208), .8)
        text(c.d, (48, 335), "微光 · 默认", 21, C["white"], True)
        text(c.d, (48, 369), "8.2 MB · 低动效 · RGB565", 12, C["muted"])
        pill(c, (54, 404, 356, 458), "正在使用", True)
    else:
        themes = [("微光", C["primary"]), ("星海", C["secondary"]), ("梦境", C["violet"]), ("点火", C["orange"])]
        for i, (name, accent) in enumerate(themes):
            col, row = i%2, i//2
            x, y = 28+col*181, 122+row*145
            rr(c.d, (x, y, x+169, y+126), 23, fill=C["surface"], outline=accent if i==0 else C["line"])
            glow(c.im, (x+84, y+55), 35, accent, 45)
            c.d = ImageDraw.Draw(c.im)
            energy_wings(c.d, (x+84, y+54), .36, accent, sam=name=="点火")
            text(c.d, (x+84, y+103), name, 14, C["white"], True, "mm")
        pill(c, (86, 418, 324, 460), "从 SD 卡导入", False)
    c.gesture()
    return c


def render_watch_weather(spec):
    mode = spec.get("mode", "fresh")
    c = WatchCanvas(title="天气", subtitle="上海 · 独立缓存可离线查看")
    if mode == "none":
        icon(c.d, "weather", (172, 165, 238, 225), C["line2"], 4)
        text(c.d, (205, 270), "还没有天气城市", 20, C["white"], True, "mm")
        text(c.d, (205, 307), "可在手机端下发，或手表配网设置", 13, C["muted"], False, "mm")
        pill(c, (72, 356, 338, 412), "设置城市", True)
    else:
        text(c.d, (51, 154), "24°", 70, C["starlight"], True, numeric=True)
        text(c.d, (53, 215), "晴间多云", 17, C["secondary"], True)
        if mode == "stale":
            pill(c, (255, 133, 366, 169), "3小时前", True, C["orange"])
            text(c.d, (366, 192), "缓存数据", 12, C["muted"], False, "ra")
        else:
            icon(c.d, "weather", (294, 137, 354, 194), C["yellow"], 4)
        stats = [("31° / 22°", "高 / 低"), ("65%", "湿度"), ("2级", "东南风")]
        for i, (v, label) in enumerate(stats):
            x = 28+i*123
            rr(c.d, (x, 252, x+108, 326), 18, fill=C["surface"], outline=C["line"])
            text(c.d, (x+54, 276), v, 15, C["white"], True, "mm", numeric=True)
            text(c.d, (x+54, 304), label, 11, C["muted"], False, "mm")
        for i, (tm, deg) in enumerate((("现在","24°"),("12时","26°"),("15时","27°"),("18时","24°"))):
            x = 34+i*92
            text(c.d, (x+38, 367), tm, 11, C["muted"], False, "mm", numeric=True)
            text(c.d, (x+38, 401), deg, 16, C["starlight"], True, "mm", numeric=True)
    c.gesture()
    return c


def render_watch_ota(spec):
    mode = spec["mode"]
    sam = mode in {"install", "rollback"}
    c = WatchCanvas(title="系统更新", subtitle="双分区 OTA · 失败可回滚", sam=sam)
    values = {
        "available": ("发现 FireflyOS 0.3.0", "新增媒体库与连接稳定性修复", .0, C["primary"]),
        "download": ("正在下载安装包", "请保持电量高于 30%", .62, C["secondary"]),
        "install": ("正在写入系统", "请勿关机，完成后自动重启", .84, C["energy"]),
        "rollback": ("更新未完成", "已安全回到 FireflyOS 0.2.1", 1.0, C["orange"]),
    }
    title_v, sub, val, accent = values[mode]
    icon(c.d, "update" if mode != "rollback" else "warning", (176, 133, 234, 195), accent, 4)
    text(c.d, (205, 239), title_v, fit_text(c.d, title_v, 340, 22, True), C["white"], True, "mm")
    text(c.d, (205, 276), sub, 13, C["muted"], False, "mm")
    if mode == "available":
        pill(c, (70, 337, 340, 395), "下载并安装 · 18.6 MB", True, accent)
        text(c.d, (205, 425), "建议连接充电器", 12, C["muted"], False, "mm")
    elif mode == "rollback":
        pill(c, (86, 337, 324, 395), "查看诊断信息", False, accent)
    else:
        c.d.rounded_rectangle((54, 340, 356, 350), 5, fill=C["line"])
        c.d.rounded_rectangle((54, 340, 54+int(302*val), 350), 5, fill=accent)
        text(c.d, (205, 384), f"{int(val*100)}%", 18, accent, True, "mm", numeric=True)
    c.gesture()
    return c


def render_watch_transfer(spec):
    c = WatchCanvas(title="文件传输", subtitle="来自 Android 伴侣 · BLE")
    icon(c.d, "download", (177, 130, 233, 190), C["secondary"], 4)
    text(c.d, (205, 229), "firefly_theme.fftheme", 18, C["white"], True, "mm", numeric=True)
    text(c.d, (205, 262), "8.2 MB · 已接收 5.1 MB", 13, C["muted"], False, "mm")
    c.d.rounded_rectangle((48, 306, 362, 316), 5, fill=C["line"])
    c.d.rounded_rectangle((48, 306, 244, 316), 5, fill=C["secondary"])
    text(c.d, (205, 353), "62%  ·  约 40 秒", 17, C["secondary"], True, "mm")
    pill(c, (86, 393, 324, 445), "取消传输", False, C["danger"])
    c.gesture()
    return c


def render_watch_guide():
    c = WatchCanvas(status=False)
    c.d.rounded_rectangle((0, 0, WATCH_W-1, WATCH_H-1), DISPLAY_RADIUS, outline=C["primary"], width=3)
    c.d.rounded_rectangle((24, 24, WATCH_W-25, WATCH_H-33), 28, outline=C["orange"], width=2)
    c.d.rectangle((40, 76, 370, 454), outline=C["secondary"], width=2)
    text(c.d, (205, 134), "物理圆角裁切", 23, C["white"], True, "mm")
    text(c.d, (205, 173), f"预设半径  {DISPLAY_RADIUS}px", 15, C["primary"], True, "mm")
    text(c.d, (205, 240), "橙色：视觉安全区", 15, C["orange"], False, "mm")
    text(c.d, (205, 278), "蓝色：主要内容区", 15, C["secondary"], False, "mm")
    text(c.d, (205, 334), "最小触控热区 48 × 48px", 15, C["starlight"], True, "mm")
    text(c.d, (205, 388), "真机遮挡测试后只需修改", 13, C["muted"], False, "mm")
    text(c.d, (205, 416), "DISPLAY_RADIUS 即可全量重绘", 13, C["muted"], False, "mm")
    return c


class AndroidCanvas:
    """432×960 companion-app preview canvas using the same visual language."""

    def __init__(self, title: str = "", subtitle: str = "", nav: str = "设备",
                 back: bool = False):
        self.im = Image.new("RGBA", (ANDROID_W, ANDROID_H), hex_rgba(C["bg"]))
        glow(self.im, (350, 145), 120, C["primary"], 28)
        glow(self.im, (52, 760), 105, C["violet"], 18)
        self.d = ImageDraw.Draw(self.im)
        text(self.d, (28, 28), "09:41", 14, C["starlight"], True, "lm", numeric=True)
        for i, h in enumerate((4, 7, 10, 13)):
            self.d.rounded_rectangle((345+i*5, 34-h, 348+i*5, 34), 1, fill=C["muted"])
        self.d.rounded_rectangle((380, 21, 407, 34), 4, outline=C["muted"], width=2)
        self.d.rectangle((408, 25, 410, 30), fill=C["muted"])
        self.d.rounded_rectangle((383, 24, 402, 31), 2, fill=C["primary"])
        if back:
            text(self.d, (28, 82), "‹", 34, C["primary"], False, "lm", numeric=True)
            title_x = 65
        else:
            title_x = 28
        if title:
            text(self.d, (title_x, 84), title, 28, C["white"], True, "lm")
        if subtitle:
            text(self.d, (28, 121), subtitle, 13, C["muted"], False, "lm")
        self.nav_active = nav

    def bottom_nav(self):
        rr(self.d, (16, 862, 416, 936), 27, fill="#081217", outline=C["line"])
        items = [("设备", "phone"), ("通知", "bell"), ("主题", "theme"), ("设置", "settings")]
        for i, (label, ico) in enumerate(items):
            x = 66+i*100
            active = label == self.nav_active
            if active:
                rr(self.d, (x-34, 875, x+34, 923), 18, fill=tint(C["primary"], 50, "#081217"))
            icon(self.d, ico, (x-10, 881, x+10, 903), C["primary"] if active else C["muted"], 2)
            text(self.d, (x, 916), label, 11, C["primary"] if active else C["muted"], active, "mm")
        self.d.rounded_rectangle((167, 945, 265, 949), 2, fill=tint(C["starlight"], 140, C["bg"]))

    def save(self, path: Path):
        self.im.putalpha(rounded_mask(self.im.size, 34))
        self.im.save(path)


def acard(c: AndroidCanvas, box, title_value="", subtitle="", ico=None,
          accent=C["primary"], right="", selected=False):
    fill = C["surface2"] if selected else C["surface"]
    rr(c.d, box, 22, fill=fill, outline=accent if selected else C["line"])
    x0, y0, x1, y1 = box
    tx = x0 + 20
    if ico:
        rr(c.d, (x0+16, y0+16, x0+60, y0+60), 14, fill=tint(accent, 32), outline=C["line"])
        icon(c.d, ico, (x0+28, y0+27, x0+48, y0+49), accent, 2)
        tx = x0 + 76
    text(c.d, (tx, y0+23), title_value, 16, C["white"], True)
    if subtitle:
        text(c.d, (tx, y0+51), subtitle, fit_text(c.d, subtitle, max(80, x1-tx-40), 12), C["muted"])
    if right:
        text(c.d, (x1-18, (y0+y1)//2), right, 13, accent, True, "rm")


def abutton(c: AndroidCanvas, box, label, active=True, accent=C["primary"]):
    rr(c.d, box, 20, fill=accent if active else C["surface"], outline=accent if active else C["line"])
    text(c.d, ((box[0]+box[2])//2, (box[1]+box[3])//2), label, 15,
         C["bg"] if active else C["white"], True, "mm")


def render_android_welcome(spec):
    c = AndroidCanvas()
    glow(c.im, (216, 280), 130, C["primary"], 55)
    c.d = ImageDraw.Draw(c.im)
    energy_wings(c.d, (216, 270), 1.25)
    text(c.d, (216, 388), "FireflyOS", 34, C["starlight"], True, "mm")
    text(c.d, (216, 433), "让手表独立，也让连接更从容", 15, C["muted"], False, "mm")
    abutton(c, (36, 710, 396, 774), "开始连接", True)
    abutton(c, (36, 792, 396, 852), "稍后再说", False)
    text(c.d, (216, 891), "手表无需手机也可正常使用", 12, C["dim"], False, "mm")
    return c


def render_android_scan(spec):
    c = AndroidCanvas("添加设备", "请让手表停留在“连接新手机”界面", back=True)
    glow(c.im, (216, 287), 110, C["secondary"], 40)
    c.d = ImageDraw.Draw(c.im)
    for r in (38, 72, 106):
        c.d.ellipse((216-r, 284-r, 216+r, 284+r), outline=tint(C["secondary"], max(35, 130-r), C["bg"]), width=2)
    icon(c.d, "watch", (194, 262, 238, 306), C["secondary"], 3)
    text(c.d, (216, 430), "正在查找附近的手表", 18, C["white"], True, "mm")
    acard(c, (28, 494, 404, 584), "FireflyOS · 2.06", "信号良好 · 未配对", "ble", C["primary"], "连接", True)
    text(c.d, (216, 638), "找不到设备？确认蓝牙与附近设备权限已开启", 12, C["muted"], False, "mm")
    return c


def render_android_pair(spec):
    c = AndroidCanvas("确认配对", "两端显示相同数字时再确认", back=True)
    rr(c.d, (40, 184, 392, 494), 32, fill=C["surface"], outline=C["secondary"], width=2)
    icon(c.d, "phone", (95, 233, 145, 291), C["secondary"], 3)
    energy_wings(c.d, (302, 262), .42)
    text(c.d, (216, 350), "482 913", 48, C["starlight"], True, "mm", numeric=True)
    text(c.d, (216, 407), "FireflyOS · FF:A2:19", 13, C["muted"], False, "mm", numeric=True)
    abutton(c, (36, 617, 396, 681), "数字一致，完成配对", True)
    abutton(c, (36, 699, 396, 759), "数字不一致", False, C["danger"])
    return c


def render_android_dashboard(spec):
    offline = spec.get("offline", False)
    c = AndroidCanvas("我的手表", "FireflyOS · ESP32-S3", nav="设备")
    accent = C["muted"] if offline else C["primary"]
    rr(c.d, (28, 156, 404, 361), 30, fill=C["surface"], outline=accent, width=2)
    glow(c.im, (112, 257), 58, accent, 40)
    c.d = ImageDraw.Draw(c.im)
    energy_wings(c.d, (112, 251), .5, accent)
    text(c.d, (196, 205), "Firefly Watch", 21, C["white"], True)
    text(c.d, (196, 241), "已断开" if offline else "已连接 · 刚刚同步", 13, accent, True)
    text(c.d, (196, 284), "--" if offline else "82%", 30, C["starlight"], True, numeric=True)
    text(c.d, (268, 287), "电量", 12, C["muted"])
    if offline:
        abutton(c, (196, 310, 374, 346), "重新连接", True)
    else:
        text(c.d, (196, 326), "预计可用 1天 8小时", 12, C["muted"])
    text(c.d, (28, 408), "快捷操作", 17, C["white"], True)
    quick = [("查找手表", "bell", C["energy"]), ("传输文件", "download", C["secondary"]),
             ("同步天气", "weather", C["yellow"]), ("设备设置", "settings", C["violet"])]
    for i, (label, ico, col) in enumerate(quick):
        x, y = 28+(i%2)*196, 445+(i//2)*116
        rr(c.d, (x, y, x+180, y+98), 22, fill=C["surface"], outline=C["line"])
        icon(c.d, ico, (x+20, y+22, x+52, y+56), col, 3)
        text(c.d, (x+68, y+38), label, 14, C["white"], True, "lm")
        text(c.d, (x+68, y+65), "不可用" if offline else "打开", 11, C["muted"])
    acard(c, (28, 700, 404, 792), "今日活动", "6,842 步 · 目标完成 68%", "activity", C["primary"], "查看")
    c.bottom_nav()
    return c


def render_android_list(spec):
    c = AndroidCanvas(spec["title"], spec.get("subtitle", ""), nav=spec.get("nav", "设置"), back=spec.get("back", True))
    rows = spec.get("rows", [])
    y = spec.get("start", 160)
    h = spec.get("height", 82)
    for row in rows:
        acard(c, (28, y, 404, y+h), row[0], row[1] if len(row)>1 else "",
              row[2] if len(row)>2 else None, row[3] if len(row)>3 else C["primary"],
              row[4] if len(row)>4 else "", row[5] if len(row)>5 else False)
        y += h+12
    if spec.get("button"):
        abutton(c, (36, min(790, y+20), 396, min(854, y+84)), spec["button"], True, spec.get("accent", C["primary"]))
    if spec.get("bottom_nav", False):
        c.bottom_nav()
    return c


def render_android_themes(spec):
    c = AndroidCanvas("主题管理", "预览后发送到手表 · 资源包受限", nav="主题")
    themes = [("微光", C["primary"], "已安装"), ("星海", C["secondary"], "12.4 MB"),
              ("梦境", C["violet"], "9.8 MB"), ("点火", C["orange"], "SAM 风格")]
    for i, (name, accent, state) in enumerate(themes):
        col, row = i%2, i//2
        x, y = 28+col*196, 159+row*260
        rr(c.d, (x, y, x+180, y+238), 26, fill=C["surface"], outline=accent if i==0 else C["line"])
        rr(c.d, (x+12, y+12, x+168, y+154), 20, fill=C["black"], outline=C["line"])
        glow(c.im, (x+90, y+83), 45, accent, 48)
        c.d = ImageDraw.Draw(c.im)
        energy_wings(c.d, (x+90, y+82), .45, accent, sam=name=="点火")
        text(c.d, (x+18, y+183), name, 16, C["white"], True)
        text(c.d, (x+18, y+211), state, 11, accent if i==0 else C["muted"])
    abutton(c, (36, 699, 396, 763), "从文件导入主题包", False)
    c.bottom_nav()
    return c


def render_android_weather(spec):
    c = AndroidCanvas("天气城市", "只下发城市与缓存，手表仍可独立查看", back=True)
    rr(c.d, (28, 162, 404, 220), 18, fill=C["surface"], outline=C["primary"])
    icon(c.d, "search", (50, 179, 76, 205), C["muted"], 2)
    text(c.d, (91, 191), "搜索城市", 14, C["muted"], False, "lm")
    acard(c, (28, 253, 404, 347), "上海", "中国 · 24°C · 晴间多云", "weather", C["primary"], "当前", True)
    acard(c, (28, 359, 404, 453), "杭州", "中国 · 25°C · 多云", "weather", C["secondary"], "选择")
    text(c.d, (28, 505), "手表获取方式", 16, C["white"], True)
    acard(c, (28, 536, 404, 630), "手机同步优先", "断开时由手表 Wi-Fi 定时更新", "phone", C["primary"], "已启用", True)
    abutton(c, (36, 736, 396, 800), "保存并立即同步", True)
    return c


def render_android_media(spec):
    c = AndroidCanvas("媒体遥控", "控制手表本地播放", back=True)
    rr(c.d, (76, 158, 356, 438), 34, fill=C["surface"], outline=C["line"])
    glow(c.im, (216, 298), 100, C["violet"], 48)
    c.d = ImageDraw.Draw(c.im)
    energy_wings(c.d, (216, 294), .9, C["violet"])
    text(c.d, (216, 491), "Fires of Firmament", 21, C["white"], True, "mm", numeric=True)
    text(c.d, (216, 527), "Firefly Watch · FLAC", 13, C["muted"], False, "mm", numeric=True)
    c.d.rounded_rectangle((46, 584, 386, 591), 3, fill=C["line"])
    c.d.rounded_rectangle((46, 584, 230, 591), 3, fill=C["primary"])
    text(c.d, (46, 616), "02:14", 12, C["muted"], numeric=True)
    text(c.d, (386, 616), "04:02", 12, C["muted"], False, "ra", numeric=True)
    icon(c.d, "pause", (184, 663, 248, 727), C["primary"], 4)
    abutton(c, (36, 784, 396, 838), "在手表上打开播放器", False)
    return c


def render_android_find(spec):
    c = AndroidCanvas("查找手表", "让手表响铃并显示 SAM 高优先级界面", back=True)
    glow(c.im, (216, 330), 150, C["energy"], 45)
    c.d = ImageDraw.Draw(c.im)
    for r in (48, 92, 138):
        c.d.ellipse((216-r, 330-r, 216+r, 330+r), outline=tint(C["energy"], 90, C["bg"]), width=2)
    icon(c.d, "bell", (183, 292, 249, 364), C["energy"], 4)
    text(c.d, (216, 517), "手表正在响铃", 22, C["white"], True, "mm")
    text(c.d, (216, 557), "距离约 3 米 · 信号良好", 13, C["muted"], False, "mm")
    abutton(c, (36, 696, 396, 764), "停止响铃", True, C["energy"])
    return c


def render_android_update(spec):
    progress = spec.get("progress")
    c = AndroidCanvas("固件更新", "安全传输 · 手表端双分区安装", back=True)
    icon(c.d, "update", (178, 181, 254, 263), C["primary"], 5)
    if progress is None:
        text(c.d, (216, 325), "FireflyOS 0.3.0", 25, C["white"], True, "mm", numeric=True)
        text(c.d, (216, 363), "当前版本 0.2.1 · 更新包 18.6 MB", 13, C["muted"], False, "mm")
        rr(c.d, (28, 415, 404, 595), 24, fill=C["surface"], outline=C["line"])
        text(c.d, (48, 448), "本次更新", 16, C["white"], True)
        text(c.d, (48, 486), "• 新增本地媒体库与录音管理", 13, C["muted"])
        text(c.d, (48, 523), "• 改善 BLE 重连与通知同步", 13, C["muted"])
        text(c.d, (48, 560), "• 优化低电量下的资源调度", 13, C["muted"])
        abutton(c, (36, 702, 396, 770), "发送到手表并安装", True)
    else:
        text(c.d, (216, 325), "正在发送更新包", 24, C["white"], True, "mm")
        text(c.d, (216, 367), "请让手机靠近手表", 13, C["muted"], False, "mm")
        c.d.rounded_rectangle((46, 452, 386, 464), 6, fill=C["line"])
        c.d.rounded_rectangle((46, 452, 46+int(340*progress), 464), 6, fill=C["primary"])
        text(c.d, (216, 515), f"{int(progress*100)}% · 约 2 分钟", 18, C["primary"], True, "mm")
        acard(c, (28, 584, 404, 678), "传输期间可离开此页面", "更新将在手表端自行完成", "info", C["secondary"], "")
        abutton(c, (36, 741, 396, 805), "取消传输", False, C["danger"])
    return c


WATCH_SPECS = [
    ("W01_boot", "启动画面", "系统与外壳", "boot", {}),
    ("W02_glance", "一瞥息屏", "系统与外壳", "glance", {}),
    ("W03_lock", "锁屏", "系统与外壳", "lock", {}),
    ("W04_home_1", "应用主页第1页", "系统与外壳", "home", {"page":1, "tiles":[("时钟","clock",C["primary"]),("日历","calendar",C["secondary"]),("活动","activity",C["energy"]),("音乐","music",C["violet"]),("天气","weather",C["yellow"]),("设置","settings",C["primary"])]}),
    ("W05_home_2", "应用主页第2页", "系统与外壳", "home", {"page":2, "tiles":[("文件","files",C["secondary"]),("录音","recorder",C["violet"]),("计算器","calculator",C["primary"]),("手电筒","flashlight",C["yellow"]),("主题","theme",C["violet"]),("更新","update",C["energy"])]}),
    ("W06_control_center", "控制中心", "系统与外壳", "control", {}),
    ("W07_notifications", "通知中心", "系统与外壳", "notifications", {}),
    ("W08_notifications_empty", "通知中心空状态", "系统与外壳", "notifications", {"empty":True}),
    ("W09_notification_detail", "通知详情", "系统与外壳", "notification_detail", {}),
    ("W10_power_menu", "电源菜单", "系统与外壳", "overlay", {"mode":"power"}),
    ("W11_charging", "充电覆盖层", "系统与外壳", "overlay", {"mode":"charging"}),
    ("W12_alarm_ringing", "闹钟响铃", "SAM高优先级", "overlay", {"mode":"alarm"}),
    ("W13_critical_battery", "严重低电量", "SAM高优先级", "overlay", {"mode":"critical"}),
    ("W14_pairing_confirm", "手表配对确认", "连接与权限", "overlay", {"mode":"pair"}),
    ("W15_pairing_result", "配对成功", "连接与权限", "overlay", {"mode":"pair_ok"}),
    ("W16_hardware_degraded", "硬件降级提示", "系统与外壳", "overlay", {"mode":"hw"}),
    ("W17_permission_prompt", "麦克风权限", "连接与权限", "overlay", {"mode":"permission"}),
    ("W18_clock_hub", "时钟中心", "时钟与日程", "clock_hub", {}),
    ("W19_alarm_list", "闹钟列表", "时钟与日程", "list", {"title":"闹钟","subtitle":"2 个闹钟 · RTC 独立唤醒","rows":[("07:30","工作日 · 微光唤醒","bell",C["primary"],"开启",True),("09:00","周末 · 柔和铃声","bell",C["violet"],"关闭")],"fab":True}),
    ("W20_alarm_editor", "闹钟编辑", "时钟与日程", "alarm_editor", {}),
    ("W21_alarm_keyboard", "闹钟名称键盘", "时钟与日程", "keyboard", {}),
    ("W22_timer_idle", "计时器待机", "时钟与日程", "timer", {}),
    ("W23_timer_running", "计时器运行", "时钟与日程", "timer", {"running":True}),
    ("W24_stopwatch", "秒表", "时钟与日程", "stopwatch", {}),
    ("W25_calendar_month", "日历月视图", "时钟与日程", "calendar", {}),
    ("W26_calendar_agenda", "日历日程视图", "时钟与日程", "calendar", {"agenda":True}),
    ("W27_settings_main", "设置主页", "设置", "list", {"title":"设置","subtitle":"本机功能优先 · 手机连接为增强","start":114,"height":58,"gap":7,"rows":[("声音与触感","音量、铃声、按键反馈","music",C["violet"],"›"),("时间与日期","RTC、时区、12/24 小时制","clock",C["primary"],"›"),("电池与电源","省电、抬腕、息屏","battery",C["energy"],"›"),("连接","蓝牙、Wi-Fi、手机伴侣","ble",C["secondary"],"›"),("系统","通知、主题、设备信息","settings",C["primary"],"›")]}),
    ("W28_settings_sound", "声音设置", "设置", "list", {"title":"声音与触感","subtitle":"硬件编解码器与扬声器","rows":[("媒体音量","当前 60%","music",C["violet"],"60%"),("闹钟音量","当前 80%","bell",C["primary"],"80%"),("触控反馈","轻微提示音","settings",C["secondary"],"开启"),("静音模式","闹钟仍可响铃","bell",C["muted"],"关闭")]}),
    ("W29_settings_time", "时间设置", "设置", "list", {"title":"时间与日期","subtitle":"内部 RTC 提供离线时间","rows":[("自动校时","连接网络或手机时同步","clock",C["primary"],"开启"),("时区","中国标准时间","weather",C["secondary"],"UTC+8"),("时间格式","状态栏与时钟统一","clock",C["violet"],"24小时"),("手动设置","断网时仍可调整","settings",C["muted"],"›")]}),
    ("W30_settings_battery", "电池设置", "设置", "list", {"title":"电池与电源","subtitle":"400mAh · 预计 1天 8小时","rows":[("省电模式","限制 Wi-Fi 与高成本动效","battery",C["energy"],"自动"),("抬腕亮屏","IMU 姿态与角速度联合判断","activity",C["primary"],"开启"),("自动息屏","无操作后关闭 AMOLED","clock",C["secondary"],"15秒"),("电池健康","当前估算容量","info",C["violet"],"良好")]}),
    ("W31_settings_display", "显示设置", "设置", "list", {"title":"显示与息屏","subtitle":"AMOLED 深色优先","rows":[("亮度","室内推荐 70%","theme",C["yellow"],"72%"),("自动亮度","使用环境传感策略","theme",C["primary"],"关闭"),("息屏样式","微光能量翼","theme",C["violet"],"›"),("防烧屏位移","静态元素轻微移动","settings",C["secondary"],"开启")]}),
    ("W32_settings_connectivity", "连接设置", "设置", "list", {"title":"连接","subtitle":"所有主要功能均可离线使用","rows":[("蓝牙","Firefly Phone 已连接","ble",C["primary"],"已连接",True),("Wi-Fi","按需开启以节省电量","wifi",C["secondary"],"已关闭"),("文件传输","等待手机或本地 SD","download",C["violet"],"打开"),("解除手机绑定","清除受信任密钥","phone",C["danger"],"›")]}),
    ("W33_settings_notifications", "通知与隐私设置", "设置", "list", {"title":"通知与隐私","subtitle":"本地最多保留 20 条","rows":[("手机通知","由 Android 伴侣按应用过滤","bell",C["primary"],"开启"),("通知预览","锁屏显示摘要","lock",C["secondary"],"开启"),("免打扰","22:30–07:00","clock",C["violet"],"自动"),("清除历史","删除本机通知缓存","warning",C["danger"],"清除")]}),
    ("W34_settings_themes", "主题设置", "设置", "list", {"title":"主题与外观","subtitle":"抽象流萤意象 · 不使用官方徽标","rows":[("当前主题","微光 · 默认","theme",C["primary"],"更改",True),("强调色","萤火青绿","theme",C["secondary"],"›"),("动效级别","低成本过渡","activity",C["violet"],"轻量"),("SAM 警报风格","高能黄绿与点火橙","warning",C["energy"],"开启")]}),
    ("W35_device_info", "设备信息", "设置", "list", {"title":"关于手表","subtitle":"FireflyOS 0.2.1","rows":[("硬件","ESP32-S3 · 2.06 AMOLED","info",C["primary"],"›"),("存储","内部 8GB · SD 32GB","files",C["secondary"],"›"),("设备地址","FF:A2:19:8C:07:41","ble",C["violet"],"复制"),("法律与许可","开源组件与协议","info",C["muted"],"›")]}),
    ("W36_diagnostics", "系统诊断", "设置", "list", {"title":"系统诊断","subtitle":"服务状态与最近异常","rows":[("显示与触控","运行正常","check",C["primary"],"正常",True),("RTC 与电源","唤醒测试通过","check",C["primary"],"正常",True),("SD 卡","32GB · 可写","files",C["secondary"],"正常"),("最近异常","OTA 已安全回滚 1 次","warning",C["orange"],"查看")]}),
    ("W37_activity_today", "今日活动", "活动与工具", "activity", {}),
    ("W38_activity_details", "活动详情", "活动与工具", "activity", {"detail":True}),
    ("W39_motion_unavailable", "运动传感器不可用", "活动与工具", "overlay", {"mode":"motion"}),
    ("W40_calculator", "计算器", "活动与工具", "calculator", {}),
    ("W41_flashlight", "屏幕手电筒", "活动与工具", "flashlight", {}),
    ("W42_files_root", "文件根目录", "存储与媒体", "list", {"title":"文件","subtitle":"内部存储与可移除 SD","rows":[("内部存储","2.1 / 8.0 GB","files",C["primary"],"›"),("SD 卡","12.4 / 32.0 GB","folder",C["secondary"],"›"),("最近接收","来自 Android 伴侣","download",C["violet"],"3 项"),("存储分析","应用、媒体与缓存","info",C["muted"],"查看")]}),
    ("W43_files_list", "文件列表", "存储与媒体", "list", {"title":"SD 卡 / 音乐","subtitle":"18 个文件 · 按名称排序","start":116,"height":61,"gap":7,"rows":[("Firefly","文件夹 · 4 项","folder",C["primary"],"›"),("Fires of Firmament.flac","28.4 MB","music",C["violet"],"⋯"),("A Gentle Light.mp3","8.6 MB","music",C["secondary"],"⋯"),("record_0629.wav","12.2 MB","recorder",C["orange"],"⋯"),("theme.fftheme","8.2 MB","theme",C["primary"],"⋯")]}),
    ("W44_sd_unavailable", "SD 卡不可用", "存储与媒体", "overlay", {"mode":"sd"}),
    ("W45_music_library", "音乐库", "存储与媒体", "music", {"mode":"library"}),
    ("W46_music_now_playing", "正在播放", "存储与媒体", "music", {"mode":"playing"}),
    ("W47_music_empty", "音乐空状态", "存储与媒体", "music", {"mode":"empty"}),
    ("W48_recorder_idle", "录音待机", "存储与媒体", "recorder", {"mode":"idle"}),
    ("W49_recorder_recording", "正在录音", "存储与媒体", "recorder", {"mode":"recording"}),
    ("W50_recordings_list", "录音列表", "存储与媒体", "recorder", {"mode":"list"}),
    ("W51_themes_gallery", "主题图库", "主题", "themes", {"mode":"gallery"}),
    ("W52_theme_detail", "主题详情", "主题", "themes", {"mode":"detail"}),
    ("W53_theme_import_error", "主题导入失败", "主题", "themes", {"mode":"error"}),
    ("W54_storage_info", "存储详情", "存储与媒体", "list", {"title":"存储空间","subtitle":"总计 40.0 GB","rows":[("应用与系统","2.8 GB","settings",C["primary"],"7%"),("音乐与录音","9.6 GB","music",C["violet"],"24%"),("主题与图片","1.4 GB","theme",C["secondary"],"4%"),("可用空间","26.2 GB","files",C["energy"],"65%",True)]}),
    ("W55_weather_fresh", "天气正常", "网络服务", "weather", {"mode":"fresh"}),
    ("W56_weather_stale", "天气缓存过期", "网络服务", "weather", {"mode":"stale"}),
    ("W57_weather_no_location", "天气无城市", "网络服务", "weather", {"mode":"none"}),
    ("W58_wifi_provision", "Wi-Fi 配网", "网络服务", "list", {"title":"配置 Wi-Fi","subtitle":"手机下发一次性加密凭据","rows":[("等待手机发送","请在 Android 伴侣中选择网络","wifi",C["secondary"],"等待",True),("按需连接","天气与 OTA 使用后自动断开","battery",C["energy"],"省电"),("隐私保护","手表不显示或回传明文密码","lock",C["primary"],"安全")]}),
    ("W59_transfer_receiving", "文件接收", "连接与传输", "transfer", {}),
    ("W60_update_available", "发现更新", "系统更新", "ota", {"mode":"available"}),
    ("W61_update_download", "下载更新", "系统更新", "ota", {"mode":"download"}),
    ("W62_update_installing", "安装更新", "系统更新", "ota", {"mode":"install"}),
    ("W63_update_rollback", "更新回滚", "系统更新", "ota", {"mode":"rollback"}),
    ("W64_find_watch", "查找手表", "SAM高优先级", "overlay", {"mode":"find"}),
    ("W65_factory_reset", "恢复出厂确认", "系统与外壳", "overlay", {"mode":"reset"}),
    ("W66_low_memory", "低内存保护", "SAM高优先级", "overlay", {"mode":"lowmem"}),
]


ANDROID_SPECS = [
    ("A01_welcome", "欢迎与未连接", "首次连接", "welcome", {}),
    ("A02_device_scan", "扫描设备", "首次连接", "scan", {}),
    ("A03_pairing_code", "配对码确认", "首次连接", "pair", {}),
    ("A04_dashboard_connected", "已连接仪表盘", "设备", "dashboard", {}),
    ("A05_dashboard_offline", "离线仪表盘", "设备", "dashboard", {"offline":True}),
    ("A06_notification_permission", "通知权限引导", "通知", "list", {"title":"启用通知同步","subtitle":"只发送你选择的应用摘要","nav":"通知","back":False,"rows":[("通知读取权限","Android 系统授权后才能同步","bell",C["primary"],"去开启",True),("隐私说明","内容经加密 BLE 发送，不上传云端","lock",C["secondary"],"查看")],"button":"下一步","bottom_nav":True}),
    ("A07_notification_filter", "通知应用筛选", "通知", "list", {"title":"通知应用","subtitle":"5 个应用允许发送到手表","nav":"通知","rows":[("日历","日程与提醒","calendar",C["primary"],"开启",True),("电话与短信","来电与短信摘要","phone",C["secondary"],"开启",True),("即时通讯","仅显示发送人和摘要","bell",C["violet"],"开启",True),("购物应用","促销与物流通知","bell",C["muted"],"关闭"),("系统通知","电池与连接状态","settings",C["energy"],"开启",True)]}),
    ("A08_settings_sync", "设备同步设置", "设置", "list", {"title":"设备设置","subtitle":"更改会排队并同步到手表","nav":"设置","rows":[("时间与时区","自动同步手机时间","clock",C["primary"],"自动",True),("天气更新","每 2 小时或手动同步","weather",C["yellow"],"2小时"),("省电策略","手表自行决定连接时机","battery",C["energy"],"平衡"),("自动重连","回到附近时恢复 BLE","ble",C["secondary"],"开启",True),("设备诊断","读取只读状态报告","info",C["violet"],"查看")]}),
    ("A09_theme_manager", "主题管理", "主题", "themes", {}),
    ("A10_weather_city", "天气城市", "天气", "weather", {}),
    ("A11_calendar_permission", "日历同步权限", "日历", "list", {"title":"同步日程摘要","subtitle":"手表只保留未来 8 条日程","rows":[("日历读取权限","选择要同步的本地日历","calendar",C["primary"],"去授权",True),("隐私范围","仅标题、时间与简短备注","lock",C["secondary"],"查看"),("断开后仍可查看","已同步内容保存在手表本地","phone",C["energy"],"支持")],"button":"允许并选择日历"}),
    ("A12_media_remote", "媒体遥控", "媒体", "media", {}),
    ("A13_find_device", "查找设备", "设备", "find", {}),
    ("A14_wifi_provision", "Wi-Fi 配网", "网络", "list", {"title":"为手表配置 Wi-Fi","subtitle":"密码仅通过加密连接发送一次","rows":[("家庭网络 5G","信号良好 · WPA2","wifi",C["primary"],"选择",True),("Firefly Lab","信号一般 · WPA2","wifi",C["secondary"],"选择"),("手动添加网络","隐藏 SSID 或企业网络","settings",C["violet"],"›")],"button":"发送到手表"}),
    ("A15_transfer_manager", "传输管理", "传输", "list", {"title":"文件传输","subtitle":"传输时手机靠近手表会更稳定","rows":[("firefly_theme.fftheme","5.1 / 8.2 MB · 62%","download",C["secondary"],"传输中",True),("Fires of Firmament.flac","等待 · 28.4 MB","music",C["violet"],"排队"),("最近完成","3 个文件 · 今天","check",C["primary"],"查看")],"button":"添加文件"}),
    ("A16_update_available", "固件更新可用", "更新", "update", {}),
    ("A17_update_progress", "固件更新传输", "更新", "update", {"progress":.62}),
    ("A18_unbind_confirm", "解除绑定确认", "设备", "list", {"title":"解除绑定？","subtitle":"此操作不会删除手表上的个人文件","rows":[("将清除的内容","受信任密钥、通知授权与同步队列","warning",C["danger"],""),("仍会保留","手表本地应用、闹钟、音乐与主题","check",C["primary"],"")],"button":"解除 Firefly Watch 绑定","accent":C["danger"]}),
]


WATCH_RENDERERS = {
    "boot": render_watch_boot, "glance": render_watch_glance, "lock": render_watch_lock,
    "home": render_watch_home, "control": render_watch_control,
    "notifications": render_watch_notifications, "notification_detail": render_watch_notification_detail,
    "overlay": render_watch_overlay, "clock_hub": render_watch_clock_hub, "list": render_watch_list,
    "alarm_editor": render_watch_alarm_editor, "keyboard": render_watch_keyboard,
    "timer": render_watch_timer, "stopwatch": render_watch_stopwatch, "calendar": render_watch_calendar,
    "activity": render_watch_activity, "calculator": render_watch_calculator,
    "flashlight": render_watch_flashlight, "music": render_watch_music,
    "recorder": render_watch_recorder, "themes": render_watch_themes,
    "weather": render_watch_weather, "ota": render_watch_ota, "transfer": render_watch_transfer,
}

ANDROID_RENDERERS = {
    "welcome": render_android_welcome, "scan": render_android_scan, "pair": render_android_pair,
    "dashboard": render_android_dashboard, "list": render_android_list, "themes": render_android_themes,
    "weather": render_android_weather, "media": render_android_media, "find": render_android_find,
    "update": render_android_update,
}


def contact_sheets(items, out_dir: Path, prefix: str, thumb_w: int, cols: int, rows: int):
    page_size = cols * rows
    for page, start in enumerate(range(0, len(items), page_size), 1):
        subset = items[start:start+page_size]
        thumb_h = int(subset[0]["height"] * thumb_w / subset[0]["width"])
        cell_w, cell_h = thumb_w + 30, thumb_h + 54
        sheet = Image.new("RGBA", (cols*cell_w+40, rows*cell_h+70), hex_rgba("#172127"))
        sd = ImageDraw.Draw(sheet)
        text(sd, (24, 24), f"FireflyOS · {prefix} · {page}/{math.ceil(len(items)/page_size)}", 22, C["starlight"], True)
        for idx, item in enumerate(subset):
            col, row = idx % cols, idx // cols
            x, y = 20+col*cell_w, 64+row*cell_h
            im = Image.open(item["path"]).convert("RGBA")
            im.thumbnail((thumb_w, thumb_h), Image.Resampling.LANCZOS)
            plate = Image.new("RGBA", im.size, hex_rgba("#071014"))
            plate.alpha_composite(im)
            sheet.alpha_composite(plate, (x+(thumb_w-im.width)//2, y))
            text(sd, (x+thumb_w//2, y+thumb_h+18), item["id"], 12, C["primary"], True, "mm", numeric=True)
            label_size = fit_text(sd, item["title"], thumb_w-8, 12, True)
            text(sd, (x+thumb_w//2, y+thumb_h+39), item["title"], label_size, C["white"], True, "mm")
        sheet.save(out_dir / f"{prefix.lower()}-contact-sheet-{page:02d}.png")


def write_gallery(manifest):
    groups = {}
    for item in manifest:
        groups.setdefault(item["platform"], []).append(item)
    cards = []
    for platform, items in groups.items():
        cards.append(f"<h2>{'手表端 · 410×502' if platform == 'watch' else 'Android 伴侣端 · 432×960'}</h2><div class='grid'>")
        for item in items:
            rel = Path(item["path"]).relative_to(OUT).as_posix()
            cards.append(f"<figure><img src='{rel}' loading='lazy'><figcaption><b>{item['id']}</b> {item['title']}<small>{item['category']}</small></figcaption></figure>")
        cards.append("</div>")
    html = """<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'><title>FireflyOS UI 全量预览</title>
<style>body{margin:0;background:#0b1115;color:#d8f7ee;font:14px 'Microsoft YaHei',sans-serif}header{position:sticky;top:0;z-index:2;padding:18px 24px;background:#0b1115ee;border-bottom:1px solid #24414a}h1{margin:0 0 6px;font-size:24px}header p{margin:0;color:#86a0a5}.wrap{padding:20px}h2{margin:28px 4px 16px}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:18px}figure{margin:0;padding:13px;background:#101b21;border:1px solid #24414a;border-radius:22px;text-align:center}img{width:100%;height:390px;object-fit:contain;background:#172127;border-radius:18px}figcaption{padding-top:10px;color:#f2fffb}figcaption b{color:#5fe7c7;margin-right:6px}small{display:block;color:#86a0a5;margin-top:5px}</style></head><body>
<header><h1>FireflyOS UI 全量预览</h1><p>圆角屏安全区 · 流萤日常态 + SAM 高优先级态 · 手机连接为增强</p></header><main class='wrap'>""" + "".join(cards) + "</main></body></html>"
    (OUT / "index.html").write_text(html, encoding="utf-8")


def write_readme(manifest):
    watch_count = sum(i["platform"] == "watch" for i in manifest)
    android_count = sum(i["platform"] == "android" for i in manifest)
    rows = ["# FireflyOS UI 全量预览", "",
            f"本目录包含 **{watch_count} 张手表端界面**、**{android_count} 张 Android 伴侣端界面**，以及联系表、圆角安全区标注图和可浏览图库。",
            "", "## 设计基线", "",
            "- 手表原生画布：410 × 502 px，RGBA PNG；物理屏圆角预设半径 44 px。",
            "- Android 伴侣端：432 × 960 px 设计预览，用于建立信息架构与视觉一致性，不代替最终 Android 适配稿。",
            "- 日常态：深黑 AMOLED 背景、萤火青绿、浅青与少量梦境淡紫。",
            "- 高优先级态：SAM 机械线条、能量黄绿、点火橙与危险红。",
            "- 关键内容避开四角裁切；按钮与主要触控项按不小于 48 × 48 px 设计。",
            "- 当前使用抽象能量翼和几何图标占位，后续可按总纲中的 AI 提示词替换为正式资产。",
            "", "## 使用方式", "",
            "- 打开 `index.html` 可逐张浏览。",
            "- `watch/` 与 `android/` 保存单张原图。",
            "- `guides/watch-safe-area.png` 用于真机校准屏幕圆角与内容安全区。",
            "- 修改 `tools/render_ui_mockups.py` 中的视觉令牌或 `DISPLAY_RADIUS`，重新运行即可全量生成。",
            "", "## 界面清单", "", "| 编号 | 平台 | 分类 | 界面 | 文件 |", "|---|---|---|---|---|"]
    for item in manifest:
        rel = Path(item["path"]).relative_to(OUT).as_posix()
        rows.append(f"| {item['id']} | {item['platform']} | {item['category']} | {item['title']} | `{rel}` |")
    rows += ["", "## 实机校准注意", "",
             "当前 44 px 圆角半径是基于 410 × 502 竖向圆角屏的保守预设。正式进入固件前，应使用纯色边框测试图在真机上测量可见像素边界，再修改半径与顶部/底部安全间距。"]
    (OUT / "README.md").write_text("\n".join(rows) + "\n", encoding="utf-8")


def main():
    for directory in (OUT, WATCH_OUT, ANDROID_OUT, GUIDE_OUT):
        directory.mkdir(parents=True, exist_ok=True)
    manifest = []
    for file_id, title_value, category, kind, data in WATCH_SPECS:
        spec = dict(data)
        spec.update({"id": file_id, "title": title_value, "category": category})
        canvas = WATCH_RENDERERS[kind](spec)
        path = WATCH_OUT / f"{file_id}.png"
        canvas.save(path)
        manifest.append({"id":file_id, "title":title_value, "category":category,
                         "platform":"watch", "width":WATCH_W, "height":WATCH_H, "path":str(path)})
    for file_id, title_value, category, kind, data in ANDROID_SPECS:
        spec = dict(data)
        spec.update({"id": file_id, "title": title_value, "category": category})
        canvas = ANDROID_RENDERERS[kind](spec)
        path = ANDROID_OUT / f"{file_id}.png"
        canvas.save(path)
        manifest.append({"id":file_id, "title":title_value, "category":category,
                         "platform":"android", "width":ANDROID_W, "height":ANDROID_H, "path":str(path)})
    guide = render_watch_guide()
    guide.save(GUIDE_OUT / "watch-safe-area.png")
    contact_sheets([i for i in manifest if i["platform"] == "watch"], OUT, "watch", 164, 4, 4)
    contact_sheets([i for i in manifest if i["platform"] == "android"], OUT, "android", 173, 3, 2)
    serializable = [{**i, "path":str(Path(i["path"]).relative_to(OUT)).replace("\\", "/")} for i in manifest]
    (OUT / "manifest.json").write_text(json.dumps(serializable, ensure_ascii=False, indent=2), encoding="utf-8")
    write_gallery(manifest)
    write_readme(manifest)
    print(f"Rendered {len(WATCH_SPECS)} watch screens and {len(ANDROID_SPECS)} Android screens to {OUT}")


if __name__ == "__main__":
    main()
