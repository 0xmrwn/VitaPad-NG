#!/usr/bin/env python3
"""Export the LiveArea images (icon0, bg, startup) from the controller SVG (UI-SPEC 9.6).

Usage:
    python3 tools/export_livearea.py [--svg SVG] [--output-root DIR] [--check]

Renders the plain controller (spr-body plus both base caps, lit groups removed) with resvg, composes
it on #0B0E14, quantises to 8-bit indexed PNG and verifies the result. Default output root is
Server/build/sce_sys (relative to the repo root). --check regenerates into a temporary directory
and compares pixel for pixel with the files in the output root.
"""
from __future__ import annotations

import argparse
import copy
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parent.parent
SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG_NS)
ET.register_namespace("xlink", "http://www.w3.org/1999/xlink")

BG = (0x0B, 0x0E, 0x14)
SCALE = 2  # render at 2x the 960x544 canvas, then downsample
MAX_BYTES = 420_000
KEEP = ("spr-body", "spr-stick-l-cap", "spr-stick-r-cap")

# name -> (relative path, size)
TARGETS = {
    "icon0": ("icon0.png", (128, 128)),
    "bg": ("livearea/contents/bg.png", (840, 500)),
    "startup": ("livearea/contents/startup.png", (280, 158)),
}


class ExportError(Exception):
    pass


def render_controller(svg: Path) -> Image.Image:
    """Plain controller on transparent, cropped to its alpha bounding box."""
    root = ET.parse(svg).getroot()
    for child in list(root):
        sid = child.get("id", "")
        if child.tag == f"{{{SVG_NS}}}g" and sid.startswith("spr-") and sid not in KEEP:
            root.remove(child)
    root.set("width", str(960 * SCALE))
    root.set("height", str(544 * SCALE))
    root.set("viewBox", "85.29411765 50 829.41176471 470")
    with tempfile.TemporaryDirectory() as td:
        src, dst = Path(td) / "c.svg", Path(td) / "c.png"
        ET.ElementTree(root).write(src, encoding="utf-8", xml_declaration=False)
        try:
            subprocess.run(["resvg", str(src), str(dst)], check=True, capture_output=True, text=True)
        except FileNotFoundError:
            raise ExportError("resvg not found on PATH")
        except subprocess.CalledProcessError as exc:
            raise ExportError(f"resvg failed: {exc.stderr.strip()}")
        im = Image.open(dst).convert("RGBA")
    bbox = im.getchannel("A").point(lambda v: 255 if v else 0).getbbox()
    if bbox is None:
        raise ExportError("controller render is empty")
    return im.crop(bbox)


def place(ctrl: Image.Image, size: tuple[int, int], width: int, left: int | None = None) -> Image.Image:
    """Controller scaled to `width` px, centred (or at x=left, vertically centred), on BG."""
    height = round(ctrl.height * width / ctrl.width)
    small = ctrl.resize((width, height), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", size, BG + (255,))
    x = (size[0] - width) // 2 if left is None else left
    canvas.alpha_composite(small, (x, (size[1] - height) // 2))
    return canvas.convert("RGB")


def make_icon(ctrl: Image.Image) -> Image.Image:
    # Corner radius 22 px; the area outside the rounded square is the same colour (the system masks it).
    img = Image.new("RGB", (128, 128), BG)
    ImageDraw.Draw(img).rounded_rectangle((0, 0, 127, 127), radius=22, fill=BG)
    img.paste(place(ctrl, (128, 128), 128 - 2 * 8))
    return img


def quantise(img: Image.Image) -> Image.Image:
    return img.convert("RGB").quantize(256, method=Image.Quantize.FASTOCTREE)


def generate(svg: Path, out_root: Path) -> None:
    ctrl = render_controller(svg)
    images = {
        "icon0": make_icon(ctrl),
        "bg": place(ctrl, TARGETS["bg"][1], round(840 * 0.6)),
        "startup": place(ctrl, TARGETS["startup"][1], 150, left=16),
    }
    for name, (rel, _) in TARGETS.items():
        path = out_root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        quantise(images[name]).save(path, format="PNG", optimize=True)


def verify(out_root: Path) -> list[str]:
    problems: list[str] = []
    for name, (rel, size) in TARGETS.items():
        path = out_root / rel
        try:
            data = path.read_bytes()
        except OSError as exc:
            problems.append(f"{path}: {exc}")
            continue
        if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
            problems.append(f"{path}: not a PNG")
            continue
        w, h = struct.unpack(">II", data[16:24])
        depth, ctype, _comp, _filt, interlace = data[24], data[25], data[26], data[27], data[28]
        if ctype != 3:
            problems.append(f"{path}: colour type {ctype}, expected 3 (indexed)")
        if depth != 8:
            problems.append(f"{path}: bit depth {depth}, expected 8")
        if interlace != 0:
            problems.append(f"{path}: interlaced")
        if (w, h) != size:
            problems.append(f"{path}: size {w}x{h}, expected {size[0]}x{size[1]}")
        if len(data) > MAX_BYTES:
            problems.append(f"{path}: {len(data)} bytes exceeds {MAX_BYTES}")
        print(f"{path}: {w}x{h}, colour type {ctype}, depth {depth}, interlace {interlace}, {len(data)} bytes")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--svg", default="Server/assets/ui/dualshock-4.svg")
    ap.add_argument("--output-root", default="Server/build/sce_sys")
    ap.add_argument("--check", action="store_true", help="compare regenerated images with the existing files")
    args = ap.parse_args()
    res = lambda p: Path(p) if Path(p).is_absolute() else REPO / p
    svg, root = res(args.svg), res(args.output_root)
    try:
        if args.check:
            with tempfile.TemporaryDirectory() as td:
                generate(svg, Path(td))
                bad = []
                for name, (rel, _) in TARGETS.items():
                    try:
                        a = Image.open(Path(td) / rel).convert("RGB")
                        b = Image.open(root / rel).convert("RGB")
                        if a.size != b.size or a.tobytes() != b.tobytes():
                            bad.append(root / rel)
                    except OSError:
                        bad.append(root / rel)
            for p in bad:
                print(f"export_livearea: --check: {p} differs from the regenerated output", file=sys.stderr)
            problems = verify(root)
            for p in problems:
                print(f"export_livearea: {p}", file=sys.stderr)
            if not bad and not problems:
                print("export_livearea: --check passed")
            return 1 if bad or problems else 0
        generate(svg, root)
        problems = verify(root)
    except (ExportError, OSError, ET.ParseError) as exc:
        print(f"export_livearea: error: {exc}", file=sys.stderr)
        return 1
    for p in problems:
        print(f"export_livearea: {p}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
