#!/usr/bin/env python3
"""Export the UI sprite atlas, sprites.json and sprites.h from the controller SVG (UI-SPEC 9.3, 9.4).

Usage:
    python3 tools/export_sprites.py [--svg SVG] [--atlas-out PNG] [--json-out JSON]
                                    [--header-out H] [--original SVG] [--check]

Paths default to the repository layout and resolve relative to the repo root (parent of tools/).
Needs Pillow and the `resvg` CLI on PATH. --check regenerates into a temporary directory and
compares against the files at the output paths (exit 1 on any difference).
"""
from __future__ import annotations

import argparse
import copy
import json
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parent.parent
SVG_NS = "http://www.w3.org/2000/svg"
XLINK_NS = "http://www.w3.org/1999/xlink"
ET.register_namespace("", SVG_NS)
ET.register_namespace("xlink", XLINK_NS)

CANVAS_W, CANVAS_H = 960, 544
VIEWBOX = "85.29411765 50 829.41176471 470"
ATLAS_W = 1024
PAD = 2
LIT_BUDGET = 41779  # 8% of 960x544 (spec 8.2)
MAX_ORIG_DIFF = 2

SPRITES = [
    "spr-body", "spr-stick-l-cap", "spr-stick-r-cap", "spr-lit-l2", "spr-lit-r2", "spr-lit-l1",
    "spr-lit-r1", "spr-lit-share", "spr-lit-options", "spr-lit-dpad-up", "spr-lit-dpad-right",
    "spr-lit-dpad-down", "spr-lit-dpad-left", "spr-lit-triangle", "spr-lit-circle", "spr-lit-cross",
    "spr-lit-square", "spr-lit-ps", "spr-lit-stick-l-ring", "spr-lit-stick-r-ring",
    "spr-lit-stick-l-cap", "spr-lit-stick-r-cap",
]


class ExportError(Exception):
    pass


def tag(name: str) -> str:
    return f"{{{SVG_NS}}}{name}"


def enum_name(sid: str) -> str:
    return "SPR_" + sid[len("spr-"):].upper().replace("-", "_")


def validate(root: ET.Element) -> None:
    ids: list[str] = []
    for child in root:
        local = child.tag.split("}")[-1]
        if local in ("title", "desc", "defs"):
            continue
        if local == "g" and child.get("id", "").startswith("spr-"):
            ids.append(child.get("id", ""))
            for node in child.iter():
                if node is not child and node.tag == tag("g") and node.get("id", "").startswith("spr-"):
                    raise ExportError(f"{node.get('id')} is nested inside {child.get('id')}")
            continue
        raise ExportError(f"disallowed top-level element <{local}> id={child.get('id')!r} (rule 9.1.1)")
    missing = [s for s in SPRITES if s not in ids]
    extra = [s for s in ids if s not in SPRITES]
    if missing:
        raise ExportError(f"missing sprite groups: {', '.join(missing)}")
    if extra:
        raise ExportError(f"unexpected sprite groups: {', '.join(extra)}")
    if ids != SPRITES:
        raise ExportError("sprite groups are not in the order of spec 8.1")


def render(root: ET.Element, keep: list[str], show: list[str], work: Path, name: str) -> Image.Image:
    """Render a copy of root keeping only the `keep` spr- groups (display removed on `show`)."""
    tree = copy.deepcopy(root)
    for child in list(tree):
        sid = child.get("id", "")
        if child.tag == tag("g") and sid.startswith("spr-"):
            if sid not in keep:
                tree.remove(child)
            elif sid in show and "display" in child.attrib:
                del child.attrib["display"]
    return render_tree(tree, work, name)


def render_tree(tree: ET.Element, work: Path, name: str) -> Image.Image:
    tree.set("width", str(CANVAS_W))
    tree.set("height", str(CANVAS_H))
    tree.set("viewBox", VIEWBOX)
    src, dst = work / f"{name}.svg", work / f"{name}.png"
    ET.ElementTree(tree).write(src, encoding="utf-8", xml_declaration=False)
    try:
        subprocess.run(["resvg", str(src), str(dst)], check=True, capture_output=True, text=True)
    except FileNotFoundError:
        raise ExportError("resvg not found on PATH")
    except subprocess.CalledProcessError as exc:
        raise ExportError(f"resvg failed for {name}: {exc.stderr.strip()}")
    im = Image.open(dst).convert("RGBA")
    if im.size != (CANVAS_W, CANVAS_H):
        raise ExportError(f"{name}: render is {im.size}, expected {CANVAS_W}x{CANVAS_H}")
    return clean(im)


def clean(im: Image.Image) -> Image.Image:
    """Set RGB to 0 where alpha is 0."""
    out = im.copy()
    out.paste((0, 0, 0, 0), mask=im.getchannel("A").point(lambda v: 255 if v == 0 else 0))
    return out


def resvg_version() -> str:
    try:
        out = subprocess.run(["resvg", "--version"], check=True, capture_output=True, text=True).stdout.strip()
    except (FileNotFoundError, subprocess.CalledProcessError):
        raise ExportError("resvg not found on PATH")
    return out if out.startswith("resvg") else f"resvg {out}"


def pack(sizes: dict[str, tuple[int, int]]) -> tuple[dict[str, tuple[int, int]], int]:
    order = sorted(sizes, key=lambda i: (-sizes[i][1], -sizes[i][0], i))
    pos: dict[str, tuple[int, int]] = {}
    x = y = shelf_h = 0
    for sid in order:
        w, h = sizes[sid]
        cw, ch = w + 2 * PAD, h + 2 * PAD
        if cw > ATLAS_W:
            raise ExportError(f"{sid} is wider than the atlas")
        if x + cw > ATLAS_W:
            y += shelf_h
            x = shelf_h = 0
        pos[sid] = (x + PAD, y + PAD)
        x += cw
        shelf_h = max(shelf_h, ch)
    height = (y + shelf_h + 7) // 8 * 8
    if height > ATLAS_W:
        raise ExportError(f"atlas height {height} exceeds {ATLAS_W}")
    return pos, height


def mean_luminance(im: Image.Image) -> float:
    """Mean relative luminance (0..1, linear light) of the opaque pixels."""
    lut = [((c / 255) / 12.92 if c / 255 <= 0.04045 else (((c / 255) + 0.055) / 1.055) ** 2.4) for c in range(256)]
    total = n = 0.0
    raw = im.tobytes()
    for i in range(0, len(raw), 4):
        r, g, b, a = raw[i:i + 4]
        if a:
            total += 0.2126 * lut[r] + 0.7152 * lut[g] + 0.0722 * lut[b]
            n += 1
    return total / n if n else 0.0


def build(svg: Path, atlas_out: Path, json_out: Path, header_out: Path, original: Path | None,
          log: bool = True) -> None:
    def say(msg: str) -> None:
        if log:
            print(msg)

    try:
        root = ET.parse(svg).getroot()
    except (OSError, ET.ParseError) as exc:
        raise ExportError(f"cannot parse {svg}: {exc}")
    validate(root)
    version = resvg_version()

    crops: dict[str, Image.Image] = {}
    origin: dict[str, tuple[int, int]] = {}
    full: dict[str, Image.Image] = {}
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        for sid in SPRITES:
            im = render(root, [sid], [sid], work, sid)
            bbox = im.getchannel("A").point(lambda v: 255 if v else 0).getbbox()
            if bbox is None:
                raise ExportError(f"{sid}: empty render")
            crops[sid] = im.crop(bbox)
            origin[sid] = (bbox[0], bbox[1])
            if sid in ("spr-body", "spr-stick-l-cap", "spr-stick-r-cap"):
                full[sid] = im

        sizes = {sid: crops[sid].size for sid in SPRITES}
        pos, atlas_h = pack(sizes)
        atlas = Image.new("RGBA", (ATLAS_W, atlas_h), (0, 0, 0, 0))
        for sid in SPRITES:
            atlas.paste(crops[sid], pos[sid])

        # Checks
        lit = SPRITES[3:]
        total = sum(sum(crops[s].getchannel("A").histogram()[1:]) for s in lit)
        pct = 100.0 * total / (CANVAS_W * CANVAS_H)
        say(f"lit area: {total} px of budget {LIT_BUDGET} ({pct:.2f}% of {CANVAS_W}x{CANVAS_H})")
        for s in lit:
            say(f"  {s}: mean luminance {mean_luminance(crops[s]):.4f}")
        if total > LIT_BUDGET:
            raise ExportError(f"lit area {total} exceeds the budget {LIT_BUDGET}")

        if original is None:
            say("original check skipped")
        else:
            try:
                oroot = ET.parse(original).getroot()
            except (OSError, ET.ParseError) as exc:
                raise ExportError(f"cannot parse {original}: {exc}")
            ref = render_tree(oroot, work, "original")
            comp = Image.new("RGBA", (CANVAS_W, CANVAS_H), (0, 0, 0, 0))
            for sid in ("spr-body", "spr-stick-l-cap", "spr-stick-r-cap"):
                comp = Image.alpha_composite(comp, full[sid])
            diff = ImageChops.difference(comp, ref)
            maxdiff = max(hi for _, hi in diff.getextrema())
            say(f"original check: maximum channel difference {maxdiff} (limit {MAX_ORIG_DIFF})")
            if maxdiff > MAX_ORIG_DIFF:
                raise ExportError(f"composite differs from the original by {maxdiff} > {MAX_ORIG_DIFF}")

    say(f"atlas {ATLAS_W}x{atlas_h}, {ATLAS_W * atlas_h * 4} bytes as RGBA8")

    rects = []
    for sid in SPRITES:
        w, h = sizes[sid]
        rects.append({"id": sid, "enum": enum_name(sid), "ax": pos[sid][0], "ay": pos[sid][1],
                      "w": w, "h": h, "sx": origin[sid][0], "sy": origin[sid][1]})

    for p in (atlas_out, json_out, header_out):
        p.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(atlas_out, format="PNG")
    with open(json_out, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"atlas": [ATLAS_W, atlas_h], "canvas": [CANVAS_W, CANVAS_H], "sprites": rects},
                  f, indent=1, sort_keys=False)
        f.write("\n")
    with open(header_out, "w", encoding="utf-8", newline="\n") as f:
        f.write(header_text(version, atlas_h, rects))
    say(f"wrote {atlas_out}, {json_out}, {header_out}")
    for r in rects:
        say(f"  {r['id']} {r['ax']} {r['ay']} {r['w']} {r['h']} {r['sx']} {r['sy']}")


def header_text(version: str, atlas_h: int, rects: list[dict]) -> str:
    L: list[str] = []
    L.append("// Generated by tools/export_sprites.py from Server/assets/ui/dualshock-4.svg. Do not edit.")
    L.append(f"// {version}, canvas {CANVAS_W}x{CANVAS_H}, viewBox {VIEWBOX}")
    L += ["#pragma once", "#include <stdint.h>", ""]
    L.append('#define SPRITES_ATLAS_PATH "app0:ui/atlas.png"')
    L.append(f"#define SPRITES_ATLAS_W {ATLAS_W}")
    L.append(f"#define SPRITES_ATLAS_H {atlas_h}")
    L.append(f"#define SPRITES_CANVAS_W {CANVAS_W}")
    L.append(f"#define SPRITES_CANVAS_H {CANVAS_H}")
    L.append(f"#define SPRITES_PAD {PAD}")
    L += ["", "typedef struct {",
          "\tuint16_t ax, ay; // top-left in the atlas",
          "\tuint16_t w, h;   // size in pixels",
          "\tint16_t sx, sy;  // top-left on the 960x544 screen (rest position for stick caps)",
          "} SpriteRect;", "", "typedef enum {"]
    for i, r in enumerate(rects):
        L.append(f"\t{r['enum']} = 0," if i == 0 else f"\t{r['enum']},")
    L += ["\tSPR_COUNT", "} SpriteId;", "",
          "extern const SpriteRect sprite_rects[SPR_COUNT];",
          "extern const char *const sprite_names[SPR_COUNT];", "",
          "#ifdef SPRITES_IMPLEMENTATION",
          "const SpriteRect sprite_rects[SPR_COUNT] = {"]
    for r in rects:
        L.append(f"\t[{r['enum']}] = {{ {r['ax']}, {r['ay']}, {r['w']}, {r['h']}, {r['sx']}, {r['sy']} }},")
    L += ["};", "const char *const sprite_names[SPR_COUNT] = {"]
    for r in rects:
        L.append(f"\t[{r['enum']}] = \"{r['id']}\",")
    L += ["};", "#endif", ""]
    return "\n".join(L)


def check(args: argparse.Namespace, svg: Path, atlas: Path, js: Path, hdr: Path, orig: Path | None) -> int:
    with tempfile.TemporaryDirectory() as td:
        t = Path(td)
        build(svg, t / "atlas.png", t / "sprites.json", t / "sprites.h", orig, log=False)
        bad = []
        for fresh, real in ((t / "sprites.h", hdr), (t / "sprites.json", js)):
            if not real.is_file() or fresh.read_bytes() != real.read_bytes():
                bad.append(real)
        try:
            a = Image.open(t / "atlas.png").convert("RGBA")
            b = Image.open(atlas).convert("RGBA")
            if a.size != b.size or a.tobytes() != b.tobytes():
                bad.append(atlas)
        except OSError:
            bad.append(atlas)
    for p in bad:
        print(f"export_sprites: --check: {p} differs from the regenerated output", file=sys.stderr)
    if not bad:
        print("export_sprites: --check passed")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--svg", default="Server/assets/ui/dualshock-4.svg")
    ap.add_argument("--atlas-out", default="Server/assets/ui/atlas.png")
    ap.add_argument("--json-out", default="Server/assets/ui/sprites.json")
    ap.add_argument("--header-out", default="Server/source/sprites.h")
    ap.add_argument("--original", default=None, help="original SVG for the composite regression check")
    ap.add_argument("--check", action="store_true", help="compare regenerated output with the existing files")
    args = ap.parse_args()
    res = lambda p: Path(p) if Path(p).is_absolute() else REPO / p
    svg, atlas, js, hdr = res(args.svg), res(args.atlas_out), res(args.json_out), res(args.header_out)
    orig = res(args.original) if args.original else None
    try:
        if args.check:
            return check(args, svg, atlas, js, hdr, orig)
        build(svg, atlas, js, hdr, orig)
    except ExportError as exc:
        print(f"export_sprites: error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
