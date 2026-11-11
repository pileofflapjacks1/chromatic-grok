#!/usr/bin/env python3
"""Host-side preview of the laptop-voice screen. UNTESTED ON HARDWARE.

Draws the 160x144 voice screen the way components/grok/grok_ui.c lays it out:
the geometry comes from the GROK_VOICE_* defines in grok_voice_util.h, the
colors from the kVoice* enum in grok_ui.c, the text from the real LVGL
unscii_8 glyph tables, and the ball from components/grok/loader/loader_*.png.
It does not run LVGL: there is no anti-aliasing to model (1 bpp font, flat
rects), but the radius of the panel corners is an approximation of LVGL's
rounded-rect rasterizer, and the Chromatic's LCD color correction is not
modelled. Also prints WCAG contrast ratios for every text/fill pair.

Needs Pillow and managed_components/ (an idf.py build fetches LVGL there).

Usage (repo root):
  python3 tools/voice_preview.py [out.png] [--scale N]
"""
from __future__ import annotations

import pathlib
import re
import sys

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[1]
GROK = ROOT / "components" / "grok"
FONT_C = ROOT / "managed_components" / "lvgl__lvgl" / "src" / "font" / "lv_font_unscii_8.c"


def defines() -> dict[str, int]:
    out = {}
    for m in re.finditer(r"#define\s+(GROK_VOICE_\w+)\s+(\d+)", (GROK / "grok_voice_util.h").read_text()):
        out[m.group(1)] = int(m.group(2))
    return out


def colors() -> dict[str, tuple[int, int, int]]:
    out = {}
    for m in re.finditer(r"(kVoice\w+)\s*=\s*0x([0-9A-Fa-f]{6})", (GROK / "grok_ui.c").read_text()):
        v = int(m.group(2), 16)
        out[m.group(1)] = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
    return out


D = defines()
C = colors()


def load_font():
    src = FONT_C.read_text()
    bm = src[src.index("glyph_bitmap[] = {"):src.index("/*---", src.index("glyph_bitmap[] = {"))]
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", bm.split("= {", 1)[1]))
    dsc = []
    gd = src[src.index("glyph_dsc[] = {"):src.index("/*---", src.index("glyph_dsc[] = {"))]
    for m in re.finditer(r"bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)", gd):
        dsc.append(tuple(int(g) for g in m.groups()))
    return data, dsc


FONT_DATA, FONT_DSC = load_font()
LINE_H = 9  # lv_font_unscii_8.line_height, base_line 0


def draw_text(img: Image.Image, x: int, cell_y: int, text: str, rgb, clip_w: int | None = None):
    px = img.load()
    for i, ch in enumerate(text):
        gid = ord(ch) - 32 + 1
        if gid < 1 or gid >= len(FONT_DSC):
            gid = ord("?") - 32 + 1
        idx, adv, bw, bh, ox, oy = FONT_DSC[gid]
        gx = x + i * 8 + ox
        gy = cell_y + LINE_H - bh - oy
        bit = 0
        for row in range(bh):
            for col in range(bw):
                byte = FONT_DATA[idx + (bit >> 3)]
                on = (byte >> (7 - (bit & 7))) & 1
                bit += 1
                X, Y = gx + col, gy + row
                if on and 0 <= X < 160 and 0 <= Y < 144:
                    if clip_w is not None and X >= x + clip_w:
                        continue
                    px[X, Y] = rgb


def rect(img, x, y, w, h, rgb):
    ImageDraw.Draw(img).rectangle([x, y, x + w - 1, y + h - 1], fill=rgb)


def rounded_panel(img, x, y, w, h, r, fill, border):
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r, fill=fill, outline=border, width=1)


def arrow(img, x, y, up, rgb):
    for row in range(4):
        width = (1 + 2 * row) if up else (7 - 2 * row)
        rect(img, x + (7 - width) // 2, y + row, width, 1, rgb)


def header(img, name, status):
    rect(img, 0, 0, 160, D["GROK_VOICE_BAR_H"], C["kVoiceAmber"])
    y = (D["GROK_VOICE_BAR_H"] - D["GROK_VOICE_PITCH"]) // 2
    draw_text(img, D["GROK_VOICE_HEAD_X"], y, name, C["kVoiceBarText"])
    draw_text(img, 160 - D["GROK_VOICE_HEAD_X"] - 8 * len(status), y, status, C["kVoiceBarText"])


def title_for(name, status):
    max_cols = D["GROK_VOICE_HEAD_COLS"] - (len(status) + 1)
    return name if len(name) <= max_cols else name[: max_cols - 1] + "."


def loader_frame(img, frame, phase):
    src = Image.open(GROK / "loader" / f"loader_{frame}.png").convert("RGB")
    px, sp = img.load(), src.load()
    accent = C["kVoiceAmber"] if phase == "thinking" else C["kVoiceCyan"]
    for y in range(144):
        for x in range(160):
            g = sp[x, y][0]
            if g == 0x14:
                px[x, y] = C["kVoiceNavy"]
            elif g == 0x46:
                px[x, y] = C["kVoiceBallDim"]
            else:
                px[x, y] = accent if y >= D["GROK_VOICE_DOT_Y"] else C["kVoiceCream"]


def wrap(text, cols):
    """Same breaking as grok_text_caption_page for plain text."""
    lines, cur = [], ""
    for word in text.split():
        while len(word) > cols:
            take = cols - len(cur)
            if take == 0:
                lines.append(cur)
                cur = ""
                continue
            lines.append((cur + word[:take]) if not cur else cur + word[:take])
            cur, word = "", word[take:]
        if cur and len(cur) + 1 + len(word) > cols:
            lines.append(cur)
            cur = ""
        cur = (cur + " " + word) if cur else word
    if cur:
        lines.append(cur)
    return lines


def caption(name, text, page):
    img = Image.new("RGB", (160, 144), C["kVoiceNavy"])
    cols, rows = D["GROK_VOICE_COLS"], D["GROK_VOICE_ROWS"]
    lines = wrap(text, cols)
    pages = max(1, (len(lines) + rows - 1) // rows)
    header(img, title_for(name, "Reply"), "Reply")
    rounded_panel(img, D["GROK_VOICE_PANEL_X"], D["GROK_VOICE_PANEL_Y"], D["GROK_VOICE_PANEL_W"],
                  D["GROK_VOICE_PANEL_H"], D["GROK_VOICE_PANEL_R"], C["kVoicePanelFill"], C["kVoiceCyan"])
    for i, ln in enumerate(lines[page * rows:(page + 1) * rows]):
        draw_text(img, D["GROK_VOICE_TEXT_X"], D["GROK_VOICE_TEXT_Y"] + i * D["GROK_VOICE_PITCH"], ln, C["kVoiceCream"])
    fy = D["GROK_VOICE_FOOTER_Y"]
    rect(img, 0, fy, 160, D["GROK_VOICE_FOOTER_H"], C["kVoiceFooterFill"])
    ty = fy + (D["GROK_VOICE_FOOTER_H"] - D["GROK_VOICE_PITCH"]) // 2
    pg = f"{page + 1}/{pages}"
    pg_x = 160 - 4 - 7 - 3 - 8 * len(pg)
    up_x = pg_x - 3 - 7
    hint = "A next B exit" if (up_x - 3) >= D["GROK_VOICE_HEAD_X"] + 8 * 13 else "A next"
    draw_text(img, D["GROK_VOICE_HEAD_X"], ty, hint, C["kVoiceLavender"])
    draw_text(img, pg_x, ty, pg, C["kVoiceCyan"])
    ay = fy + (D["GROK_VOICE_FOOTER_H"] - 4) // 2
    arrow(img, up_x, ay, True, C["kVoiceCyan"] if page > 0 else C["kVoiceDim"])
    arrow(img, 160 - 4 - 7, ay, False, C["kVoiceCyan"] if page + 1 < pages else C["kVoiceDim"])
    return img, lines


def ball(name, status, frame):
    img = Image.new("RGB", (160, 144), C["kVoiceNavy"])
    loader_frame(img, frame, status.lower())
    header(img, title_for(name, status), status)
    return img


def lum(rgb):
    def ch(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = rgb
    return 0.2126 * ch(r) + 0.7152 * ch(g) + 0.0722 * ch(b)


def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


PAIRS = [
    ("header text on amber bar", "kVoiceBarText", "kVoiceAmber"),
    ("reply text on panel", "kVoiceCream", "kVoicePanelFill"),
    ("footer hint on footer bar", "kVoiceLavender", "kVoiceFooterFill"),
    ("page number on footer bar", "kVoiceCyan", "kVoiceFooterFill"),
    ("panel border on screen", "kVoiceCyan", "kVoiceNavy"),
    ("ball on screen", "kVoiceCream", "kVoiceNavy"),
    ("listening dots on screen", "kVoiceCyan", "kVoiceNavy"),
    ("thinking dots on screen", "kVoiceAmber", "kVoiceNavy"),
    ("unavailable arrow on footer bar (decorative, meant to be dim)", "kVoiceDim", "kVoiceFooterFill"),
]


def main(argv):
    scale = 4
    args = [a for a in argv[1:] if not a.startswith("--")]
    for a in argv[1:]:
        if a.startswith("--scale="):
            scale = int(a.split("=", 1)[1])
    out = pathlib.Path(args[0]) if args else pathlib.Path("voice5-preview.png")
    sample = ("You can do it! Start with one small step today, then stack another tomorrow. "
              "Keep the streak alive, rest when you need to, and check back with me tomorrow "
              "so we can look at how the week went and plan the next one together.")
    p0, lines = caption("Carty G", sample, 0)
    p1, _ = caption("Carty G", sample, 1)
    shots = [ball("Carty G", "Listening", 0), ball("Carty G", "Thinking", 3), p0, p1]
    long_name = caption("Chromatic Add-ons", "Short one.", 0)[0]
    shots.append(long_name)
    gap = 6
    W = len(shots) * 160 * scale + (len(shots) + 1) * gap * scale // 2
    sheet = Image.new("RGB", (W, 144 * scale + gap * scale), (60, 60, 60))
    x = gap * scale // 2
    for s in shots:
        sheet.paste(s.resize((160 * scale, 144 * scale), Image.NEAREST), (x, gap * scale // 2))
        x += 160 * scale + gap * scale // 2
    sheet.save(out)
    print("wrote", out, f"({len(lines)} wrapped rows, {D['GROK_VOICE_COLS']}x{D['GROK_VOICE_ROWS']} per page)")
    for label, fg, bg in PAIRS:
        print(f"  {contrast(C[fg], C[bg]):5.1f}:1  {label}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
