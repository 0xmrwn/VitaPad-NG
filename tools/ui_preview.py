#!/usr/bin/env python3
"""Render UI draw lists to PNG frames and a contact sheet, emulating the Vita compositor (UI-SPEC 15.2).

Usage:
    python3 tools/ui_preview.py --out OUTDIR [--zones FILE] [--plan-dump PATH] [--from-json FILE ...]

Without --from-json it runs `plan_dump <scenario> [--zones FILE]` for every scenario in
tools/ui_scenarios.txt (build it with `make -C Server/tests`). With --from-json it renders the
given draw-list JSON files instead. Frames are 960x544 black RGB; sprites come from
Server/assets/ui/atlas.png and sprites.json. Text uses a macOS system font and is approximate
(the Vita's own PGF font is not available on a Mac).
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parent.parent
W, H = 960, 544
FONT_PATHS = ["/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/SFNS.ttf"]
KIND = {"sprite": 0, "line": 1, "text": 2}
_font_cache: dict[int, ImageFont.ImageFont | ImageFont.FreeTypeFont] = {}


def get_font(size: int):
    size = max(1, size)
    if size not in _font_cache:
        font = None
        for p in FONT_PATHS:
            try:
                font = ImageFont.truetype(p, size)
                break
            except OSError:
                continue
        if font is None:
            try:
                font = ImageFont.load_default(size)
            except TypeError:
                font = ImageFont.load_default()
        _font_cache[size] = font
    return _font_cache[size]


class Atlas:
    def __init__(self, png: Path, js: Path):
        self.img = Image.open(png).convert("RGBA")
        data = json.loads(js.read_text(encoding="utf-8"))
        self.rects = data["sprites"]
        self.by_name: dict[str, int] = {}
        for i, r in enumerate(self.rects):
            self.by_name[r["id"]] = i
            self.by_name[r["enum"]] = i
            self.by_name[r["enum"].lower()] = i

    def index(self, v) -> int:
        if isinstance(v, int):
            i = v
        elif isinstance(v, str) and v.lstrip("-").isdigit():
            i = int(v)
        elif v in self.by_name:
            i = self.by_name[v]
        else:
            raise ValueError(f"unknown sprite {v!r}")
        if not 0 <= i < len(self.rects):
            raise ValueError(f"sprite index {i} out of range")
        return i

    def sprite(self, v) -> Image.Image:
        r = self.rects[self.index(v)]
        return self.img.crop((r["ax"], r["ay"], r["ax"] + r["w"], r["ay"] + r["h"]))


def rgba(color: int) -> tuple[int, int, int, int]:
    return color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, (color >> 24) & 0xFF


def over(canvas: Image.Image, layer: Image.Image, x: int, y: int) -> None:
    """Straight-alpha over of `layer` at (x, y), clipped to the canvas."""
    x0, y0 = max(0, -x), max(0, -y)
    x1, y1 = min(layer.width, canvas.width - x), min(layer.height, canvas.height - y)
    if x1 <= x0 or y1 <= y0:
        return
    part = layer.crop((x0, y0, x1, y1))
    canvas.alpha_composite(part, (x + x0, y + y0))


def parse_kind(v) -> int:
    if isinstance(v, str):
        if v.lower() in KIND:
            return KIND[v.lower()]
        if v.isdigit():
            return int(v)
        raise ValueError(f"unknown kind {v!r}")
    return int(v)


def draw_item(canvas: Image.Image, atlas: Atlas, it: dict) -> None:
    kind = parse_kind(it["kind"])
    color = it.get("color", 0xFFFFFFFF)
    if isinstance(color, str):
        color = int(color, 0)
    r, g, b, a = rgba(int(color))
    x, y = int(it.get("x", 0)), int(it.get("y", 0))
    if kind == 0:
        spr = atlas.sprite(it["sprite"])
        tint = Image.new("RGBA", spr.size, (r, g, b, a))
        over(canvas, ImageChops.multiply(spr, tint), x, y)
    elif kind == 1:
        x1, y1 = int(it.get("x1", 0)), int(it.get("y1", 0))
        lx0, ly0 = min(x, x1), min(y, y1)
        lw, lh = abs(x1 - x) + 1, abs(y1 - y) + 1
        layer = Image.new("RGBA", (lw, lh), (0, 0, 0, 0))
        ImageDraw.Draw(layer).line((x - lx0, y - ly0, x1 - lx0, y1 - ly0), fill=(r, g, b, a), width=1)
        over(canvas, layer, lx0, ly0)
    elif kind == 2:
        text = it.get("text", "")
        if not text or a == 0:
            return
        size = round(17 * float(it.get("scale", 1.0)))
        maxw = int(it.get("x1", 0))
        font = get_font(size)
        width = font.getlength(text)
        if maxw > 0 and width > maxw:
            size = max(1, int(size * maxw / width))
            font = get_font(size)
            while size > 1 and font.getlength(text) > maxw:
                size -= 1
                font = get_font(size)
            width = font.getlength(text)
        left = x - width / 2 if it.get("align") in (1, "center") else x
        pad = size
        layer = Image.new("RGBA", (int(width) + 2 * pad, size * 2 + 2 * pad), (0, 0, 0, 0))
        ImageDraw.Draw(layer).text((pad, size + pad), text, font=font, fill=(r, g, b, a), anchor="ls")
        over(canvas, layer, round(left) - pad, y - size - pad)


def render_frame(atlas: Atlas, items: list[dict]) -> Image.Image:
    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    for it in items:
        draw_item(canvas, atlas, it)
    return canvas.convert("RGB")


def frame_stats(frame: Image.Image) -> tuple[float, float]:
    gray = frame.convert("L")
    hist = gray.histogram()
    mean = sum(i * c for i, c in enumerate(hist)) / (W * H)
    rgb_nonblack = ImageChops.lighter(ImageChops.lighter(*frame.split()[:2]), frame.split()[2])
    nb = W * H - rgb_nonblack.histogram()[0]
    return mean, 100.0 * nb / (W * H)


def contact_sheet(frames: list[tuple[str, int, Image.Image]]) -> Image.Image:
    cols = 3
    cw, ch, cap, gap = W // 2, H // 2, 22, 8
    rows = (len(frames) + cols - 1) // cols
    footer = 26
    sheet = Image.new("RGB", (cols * (cw + gap) + gap, rows * (ch + cap + gap) + gap + footer), (28, 28, 32))
    d = ImageDraw.Draw(sheet)
    font = get_font(13)
    for i, (name, cost, im) in enumerate(frames):
        x = gap + (i % cols) * (cw + gap)
        y = gap + (i // cols) * (ch + cap + gap)
        d.text((x, y + 15), f"{name}  cost {cost}", font=font, fill=(200, 205, 214), anchor="ls")
        sheet.paste(im.resize((cw, ch), Image.Resampling.LANCZOS), (x, y + cap))
        d.rectangle((x - 1, y + cap - 1, x + cw, y + cap + ch), outline=(60, 64, 72))
    d.text((gap, sheet.height - 8), "Text is approximate: macOS system font, not the Vita PGF font (metrics and widths differ).",
           font=get_font(12), fill=(150, 156, 168), anchor="ls")
    return sheet


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--zones", help="zones.txt passed to plan_dump")
    ap.add_argument("--plan-dump", default="Server/tests/out/plan_dump")
    ap.add_argument("--from-json", action="append", default=[], metavar="FILE",
                    help="render this draw-list JSON instead of running plan_dump (repeatable)")
    args = ap.parse_args()
    res = lambda p: Path(p) if Path(p).is_absolute() else REPO / p
    out = Path(args.out)

    lists: list[dict] = []
    if args.from_json:
        for f in args.from_json:
            try:
                data = json.loads(Path(f).read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as exc:
                print(f"ui_preview: cannot read {f}: {exc}", file=sys.stderr)
                return 1
            data.setdefault("scenario", Path(f).stem)
            lists.append(data)
    else:
        pd = res(args.plan_dump)
        if not pd.is_file():
            print(f"ui_preview: {pd} not found. Run `make -C Server/tests` to build plan_dump first.", file=sys.stderr)
            return 2
        names = [n.strip() for n in (REPO / "tools/ui_scenarios.txt").read_text().splitlines() if n.strip()]
        for n in names:
            cmd = [str(pd), n] + (["--zones", args.zones] if args.zones else [])
            try:
                p = subprocess.run(cmd, check=True, capture_output=True, text=True)
                lists.append(json.loads(p.stdout))
            except (subprocess.CalledProcessError, json.JSONDecodeError) as exc:
                detail = getattr(exc, "stderr", "") or str(exc)
                print(f"ui_preview: plan_dump {n} failed: {detail.strip()}", file=sys.stderr)
                return 1

    try:
        atlas = Atlas(REPO / "Server/assets/ui/atlas.png", REPO / "Server/assets/ui/sprites.json")
    except (OSError, KeyError, json.JSONDecodeError) as exc:
        print(f"ui_preview: cannot load the atlas ({exc}); run tools/export_sprites.py", file=sys.stderr)
        return 1

    out.mkdir(parents=True, exist_ok=True)
    frames: list[tuple[str, int, Image.Image]] = []
    for data in lists:
        name = data["scenario"]
        try:
            frame = render_frame(atlas, data["items"])
        except (ValueError, KeyError) as exc:
            print(f"ui_preview: {name}: bad draw list: {exc}", file=sys.stderr)
            return 1
        frame.save(out / f"{name}.png")
        cost = data.get("cost", 0)
        mean, nb = frame_stats(frame)
        print(f"{name}: cost {cost}, mean luminance {mean:.2f}, non-black {nb:.2f}%")
        frames.append((name, cost, frame))
    contact_sheet(frames).save(out / "contact-sheet.png")
    print(f"wrote {len(frames)} frames and {out / 'contact-sheet.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
