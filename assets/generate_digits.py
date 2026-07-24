#!/usr/bin/env python3
"""Bake the nixie digit emission sprites (0-9).

A nixie numeral is a thin bent-wire cathode that glows: a hot near-white core,
a bright orange inner halo, an orange mid-bloom, and a deep-red outer halo that
spills well past the wire.  We render each digit as EMISSION (additive light over
black); at runtime it is composited as

    visible = base_plate + emission * transmittance

so the transmittance map (glass + honeycomb mesh, air=1 outside the tube) does the
occlusion and lets the outer halo bleed onto the bezel.  Nothing here is clipped
to the glass -- the sprite cell is deliberately padded so the red halo survives.

Digits are authored clean-room as coarse control polylines (see GLYPHS), smoothed
with Chaikin corner-cutting into organic wire, then given the glow passes.  All 6
tubes are ~identical in size, so one canonical cell is baked per digit and reused
at each tube position.

Usage:
    python3 generate_digits.py WIDTH HEIGHT [--variant NAME] [--demo HH:MM:SS] [--plate PLATE.png]

--variant selects the tube layout (default hhmmss; see nixie_variants.py). Writes
derived/digits-<W>x<H>/digit_0.png .. digit_9.png (RGB emission cells) and
digits-<W>x<H>/meta.json (cell size + placement) for hhmmss, or
derived/digits-<variant>-<W>x<H>/... otherwise. With --demo, also composites a
full-frame validation render over --plate using the matching transmittance file.
"""
import sys
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

import nixie_variants as variants

HERE = Path(__file__).resolve().parent
DERIVED = HERE / "derived"

SRC_MASKS = None   # set in main() once --variant is parsed
TUBE_MASKS = None  # ditto
BULB_MASKS = None  # ditto

# --- material parameters (expressed at the 3840x2160 master; scaled by H/2160) ---
MASTER_H = 2160.0
P = dict(
    digit_fill_w=0.60,   # digit width  as a fraction of the tube's width (IN-14 is wide/chunky)
    digit_fill_h=0.68,   # digit height as a fraction of the tube's height
    cell_pad=0.55,       # sprite margin each side, as a fraction of tube width
    wire_w=7.6,          # core wire diameter in px @ master (IN-14 has a thick cathode)
    smooth_iters=2,      # Chaikin passes; fewer = crisper bends (bent-wire, not blobby)
    # glow passes: (blur sigma px @ master, RGB color, weight)
    passes=[
        (0.0,   (255, 214, 150), 0.75),  # hot core, warm (not white) so orange wins
        (3.0,   (255, 150,  50), 1.05),  # inner halo, strong neon orange
        (8.0,   (255,  96,  10), 0.95),  # mid bloom (~#FF6A00 band)
        (20.0,  (255,  64,   0), 0.78),  # orange spill
        (52.0,  (180,  26,   0), 0.52),  # deep-red outer halo (bleeds past glass)
    ],
    # cathode-stack depth: the 10 digits sit at different depths, so each reads at a
    # slightly different scale (closer digit -> larger). Scale grows with digit value.
    depth_back=0.955,    # scale of the backmost digit (value 0)
    depth_front=1.045,   # scale of the frontmost digit (value 9)
    # faint always-on emission of the UNLIT cathode wires stacked behind the lit one
    ghost_passes=[
        (0.0, (48, 21, 8), 0.40),   # dim warm wire core
        (5.0, (34, 15, 5), 0.28),   # faint halo so the tangle reads softly
    ],
)


def depth_scale(digit):
    t = int(digit) / 9.0
    return P["depth_back"] + (P["depth_front"] - P["depth_back"]) * t


def chaikin(points, closed, iters=3):
    """Corner-cutting subdivision -> smooth organic wire from coarse control pts."""
    pts = [np.asarray(p, np.float64) for p in points]
    for _ in range(iters):
        out = []
        n = len(pts)
        segs = range(n) if closed else range(n - 1)
        if not closed:
            out.append(pts[0])
        for i in segs:
            a = pts[i]
            b = pts[(i + 1) % n]
            out.append(0.75 * a + 0.25 * b)
            out.append(0.25 * a + 0.75 * b)
        if not closed:
            out.append(pts[-1])
        pts = out
    return pts


# Clean-room numeral control polylines in a normalised box, x:[0,1] L->R, y:[0,1]
# top->bottom.  Each entry: list of (points, closed?) strokes.  Chaikin rounds them.
GLYPHS = {
    "0": [([(0.50, 0.03), (0.83, 0.20), (0.87, 0.50), (0.83, 0.80),
            (0.50, 0.97), (0.17, 0.80), (0.13, 0.50), (0.17, 0.20)], True)],
    # 1: IN-14 style -- a near-bare vertical rod with a short top-left flag, NO foot.
    "1": [([(0.40, 0.22), (0.58, 0.07), (0.58, 0.93)], False)],
    "2": [([(0.16, 0.30), (0.30, 0.09), (0.60, 0.05), (0.80, 0.22),
            (0.77, 0.45), (0.22, 0.90), (0.18, 0.94), (0.84, 0.94)], False)],
    "3": [([(0.17, 0.24), (0.42, 0.06), (0.72, 0.12), (0.81, 0.31),
            (0.58, 0.48), (0.47, 0.50), (0.62, 0.52), (0.83, 0.66),
            (0.75, 0.90), (0.45, 0.97), (0.16, 0.82)], False)],
    # 4: IN-14 open-top form -- right vertical, plus a diagonal to a crossbar; the
    # diagonal and the vertical do NOT meet at top (open notch).
    "4": [([(0.70, 0.06), (0.70, 0.94)], False),
          ([(0.58, 0.06), (0.12, 0.64), (0.90, 0.64)], False)],
    "5": [([(0.80, 0.07), (0.30, 0.07), (0.25, 0.44), (0.52, 0.37),
            (0.75, 0.46), (0.81, 0.70), (0.66, 0.92), (0.36, 0.96),
            (0.16, 0.82)], False)],
    "6": [([(0.75, 0.10), (0.44, 0.05), (0.22, 0.30), (0.15, 0.62),
            (0.24, 0.87), (0.50, 0.96), (0.75, 0.86), (0.83, 0.63),
            (0.72, 0.45), (0.46, 0.39), (0.23, 0.52)], False)],
    # 7: single continuous wire, NO crossbar (a real nixie digit is one cathode;
    # a crossbar would be a disconnected segment that cannot illuminate).
    "7": [([(0.15, 0.08), (0.85, 0.08), (0.42, 0.94)], False)],
    # 8: continuous figure-eight -- the two loops SHARE a waist bar (the bottom of
    # the top loop IS the top of the bottom loop), not two loops pinched to a point.
    "8": [([(0.30, 0.48), (0.27, 0.27), (0.40, 0.10), (0.60, 0.10),
            (0.73, 0.27), (0.70, 0.48)], True),
          ([(0.30, 0.48), (0.24, 0.67), (0.30, 0.88), (0.50, 0.97),
            (0.70, 0.88), (0.76, 0.67), (0.70, 0.48)], True)],
    "9": [([(0.25, 0.90), (0.56, 0.95), (0.78, 0.70), (0.85, 0.38),
            (0.76, 0.13), (0.50, 0.04), (0.26, 0.14), (0.17, 0.37),
            (0.28, 0.55), (0.54, 0.61), (0.77, 0.48)], False)],
}


def load_mask_alpha(name, w, h):
    img = Image.open(SRC_MASKS / name).convert("RGBA").resize((w, h), Image.LANCZOS)
    return np.asarray(img)[:, :, 3].astype(np.float32) / 255.0


def bbox_of(alpha):
    ys, xs = np.where(alpha > 0.02)
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def tube_boxes(w, h):
    boxes = []
    for name in TUBE_MASKS:
        a = load_mask_alpha(name, w, h)
        x0, y0, x1, y1 = bbox_of(a)
        boxes.append((int(x0), int(y0), int(x1 - x0), int(y1 - y0)))  # x,y,w,h
    return boxes


def render_wire(cell_w, cell_h, dw, dh, wire_px, digit):
    """Rasterise a glyph's smoothed wire (size dw x dh, centred in the cell) as an
    intensity map (0..1), supersampled."""
    ss = 2  # supersample for smooth wire edges
    W, H = cell_w * ss, cell_h * ss
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    lw = max(1, int(round(wire_px * ss)))
    r = lw / 2.0
    ox = (cell_w - dw) / 2.0
    oy = (cell_h - dh) / 2.0
    for points, closed in GLYPHS[str(digit)]:
        sm = chaikin(points, closed, iters=P["smooth_iters"])
        px = [((ox + x * dw) * ss, (oy + y * dh) * ss) for x, y in sm]
        if closed:
            px.append(px[0])
        d.line(px, fill=255, width=lw, joint="curve")
        # round the caps/joins so the wire reads as continuous bent metal
        for (x, y) in px:
            d.ellipse([x - r, y - r, x + r, y + r], fill=255)
    img = img.resize((cell_w, cell_h), Image.LANCZOS)
    return np.asarray(img, np.float32) / 255.0


def glow_from_wire(wire, scale, passes):
    """Turn a wire intensity map into RGB emission via the given blur/colour passes."""
    wimg = Image.fromarray((wire * 255).astype(np.uint8), "L")
    emission = np.zeros((*wire.shape, 3), np.float32)
    for sigma, color, weight in passes:
        layer = wire if sigma <= 0.0 else np.asarray(
            wimg.filter(ImageFilter.GaussianBlur(sigma * scale)), np.float32) / 255.0
        emission += weight * layer[:, :, None] * np.array(color, np.float32)[None, None, :]
    return emission


def bake_digit(digit, cell_w, cell_h, digit_w, digit_h, scale, dscale):
    wire = render_wire(cell_w, cell_h, digit_w * dscale, digit_h * dscale, P["wire_w"] * scale, digit)
    emission = glow_from_wire(wire, scale, P["passes"])
    return np.clip(emission, 0, 255).astype(np.uint8)


def add_cell(dst, cell, cx, cy):
    """Add a cell into a full-frame array centred at (cx, cy), clipped to bounds."""
    fh, fw = dst.shape[:2]
    ch, cw = cell.shape[:2]
    x0, y0 = cx - cw // 2, cy - ch // 2
    dx0, dy0 = max(0, x0), max(0, y0)
    dx1, dy1 = min(fw, x0 + cw), min(fh, y0 + ch)
    if dx1 <= dx0 or dy1 <= dy0:
        return
    sx0, sy0 = dx0 - x0, dy0 - y0
    dst[dy0:dy1, dx0:dx1] += cell[sy0:sy0 + (dy1 - dy0), sx0:sx0 + (dx1 - dx0)]


def ghost_stack_cell(cell_w, cell_h, digit_w, digit_h, scale):
    """Faint emission of ALL ten unlit cathode wires stacked at their depths -- the
    wire tangle visible behind the lit digit (and filling the off tubes)."""
    acc = np.zeros((cell_h, cell_w, 3), np.float32)
    for D in range(10):
        wire = render_wire(cell_w, cell_h, digit_w * depth_scale(D), digit_h * depth_scale(D),
                           P["wire_w"] * scale, str(D))
        acc += glow_from_wire(wire, scale, P["ghost_passes"])
    return acc


# separator neon bulb (INS-1 style): the glow is a small vertical filament near the
# bulb centre, NOT a uniform fill -- the glass shows around it and the glow spills soft.
BULB = dict(
    cx_frac=0.50, cy_frac=0.56,   # glow centre within the bulb bbox (slightly low)
    sx_frac=0.15, sy_frac=0.24,   # gaussian radii (fraction of bulb w/h) -> vertical filament
    passes=[                      # (blur sigma px @ master, RGB, weight)
        (0.0,  (255, 176,  92), 1.00),  # bright warm filament core
        (4.0,  (255, 116,  38), 0.80),  # inner neon-orange
        (12.0, (255,  72,  16), 0.60),  # bloom
        (30.0, (196,  40,   6), 0.42),  # soft outer spill (escapes the glass)
    ],
)


def bulb_emission(w, h, scale):
    """Full-frame emission of the always-on separator neon bulbs: a concentrated
    filament glow per bulb (not a 100% fill). Bulbs sit in air (transmittance ~1)."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    core = np.zeros((h, w), np.float32)
    for (bx, by, bw, bh) in bulb_boxes(w, h):
        nx = (xx - (bx + BULB["cx_frac"] * bw)) / (BULB["sx_frac"] * bw)
        ny = (yy - (by + BULB["cy_frac"] * bh)) / (BULB["sy_frac"] * bh)
        core = np.maximum(core, np.exp(-(nx * nx + ny * ny)))
    core[core < 0.01] = 0.0
    cimg = Image.fromarray((core * 255).astype(np.uint8), "L")
    emission = np.zeros((h, w, 3), np.float32)
    for sigma, color, weight in BULB["passes"]:
        layer = core if sigma <= 0 else np.asarray(
            cimg.filter(ImageFilter.GaussianBlur(sigma * scale)), np.float32) / 255.0
        emission += weight * layer[:, :, None] * np.array(color, np.float32)[None, None, :]
    return emission


def bulb_boxes(w, h):
    boxes = []
    for name in BULB_MASKS:
        a = load_mask_alpha(name, w, h)
        x0, y0, x1, y1 = bbox_of(a)
        boxes.append((int(x0), int(y0), int(x1 - x0), int(y1 - y0)))
    return boxes


def main():
    global SRC_MASKS, TUBE_MASKS, BULB_MASKS
    variant, argv = variants.pop_variant(sys.argv)
    if len(argv) < 3:
        print(__doc__)
        return 2
    w, h = int(argv[1]), int(argv[2])
    scale = h / MASTER_H
    SRC_MASKS = variants.masks_dir(variant)
    TUBE_MASKS = variants.tube_masks(variant)
    BULB_MASKS = variants.bulb_masks(variant)
    boxes = tube_boxes(w, h)
    tw = int(round(np.median([b[2] for b in boxes])))
    th = int(round(np.median([b[3] for b in boxes])))

    pad = int(round(P["cell_pad"] * tw))
    cell_w = tw + 2 * pad
    cell_h = th + 2 * pad
    digit_w = P["digit_fill_w"] * tw
    digit_h = P["digit_fill_h"] * th

    out_dir = DERIVED / f"{variants.tag(variant, 'digits')}-{w}x{h}"
    out_dir.mkdir(parents=True, exist_ok=True)
    for D in range(10):
        emis = bake_digit(str(D), cell_w, cell_h, digit_w, digit_h, scale, depth_scale(D))
        Image.fromarray(emis, "RGB").save(out_dir / f"digit_{D}.png")
    # always-on layer: warm separator bulbs + the faint unlit cathode stack per tube
    static = bulb_emission(w, h, scale)
    ghost = ghost_stack_cell(cell_w, cell_h, digit_w, digit_h, scale)
    for (bx, by, bw, bh) in boxes:
        add_cell(static, ghost, bx + bw // 2, by + bh // 2)
    static_emis = np.clip(static, 0, 255).astype(np.uint8)
    Image.fromarray(static_emis, "RGB").save(out_dir / "static_emission.png")

    meta = dict(width=w, height=h, cell_w=cell_w, cell_h=cell_h, pad=pad,
                tube_w=tw, tube_h=th, tube_boxes=boxes, bulb_boxes=bulb_boxes(w, h))
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2))
    print(f"wrote {out_dir.relative_to(HERE)}/digit_0..9.png + static_emission.png  cell={cell_w}x{cell_h}")

    if "--demo" in argv:
        demo(argv[argv.index("--demo") + 1], out_dir, boxes, w, h, variant, argv)
    return 0


def demo(timestr, out_dir, boxes, w, h, variant, argv):
    digits = [c for c in timestr if c.isdigit()]
    if len(digits) != len(boxes):
        print(f"  --demo expects {len(boxes)} digits for variant {variant!r}, got {timestr!r}")
        return
    plate_path = None
    if "--plate" in argv:
        plate_path = Path(argv[argv.index("--plate") + 1])
    else:
        cand = variants.plate_path(variant, w, h)
        if cand.exists():
            plate_path = cand
    if plate_path is None or not plate_path.exists():
        print("  --demo needs --plate PLATE.png (or a matching base plate)")
        return
    tpath = DERIVED / f"{variants.tag(variant, 'transmittance')}-{w}x{h}.png"
    if not tpath.exists():
        print(f"  missing {tpath.name}; run generate_transmittance.py {w} {h} --variant {variant} first")
        return

    plate = np.asarray(Image.open(plate_path).convert("RGB").resize((w, h), Image.LANCZOS), np.float32)
    trans = np.asarray(Image.open(tpath).convert("L").resize((w, h), Image.LANCZOS), np.float32) / 255.0

    # layered model (draw order backing -> digits -> mesh -> glass):
    #   visible = plate + transmittance * (backing + emission)
    # transmittance (mesh*glass) occludes the SUM, so the mesh sits in front of both
    # the backing and the digits; air=1 outside the tubes preserves the glow spill.
    interior = np.zeros((h, w, 3), np.float32)
    bpath = DERIVED / f"{variants.tag(variant, 'backing')}-{w}x{h}.png"
    if bpath.exists():
        b = np.asarray(Image.open(bpath).convert("RGBA").resize((w, h), Image.LANCZOS), np.float32)
        interior += b[:, :, :3]  # backing rgb is premultiplied by mask coverage

    for ch, (bx, by, bw, bh) in zip(digits, boxes):
        cell = np.asarray(Image.open(out_dir / f"digit_{ch}.png").convert("RGB"), np.float32)
        add_cell(interior, cell, bx + bw // 2, by + bh // 2)

    # always-on bulbs + faint unlit cathode stacks (baked into static_emission)
    spath = out_dir / "static_emission.png"
    if spath.exists():
        interior += np.asarray(Image.open(spath).convert("RGB"), np.float32)

    vis = np.clip(plate + interior * trans[:, :, None], 0, 255).astype(np.uint8)
    pw = 1920
    prev = Image.fromarray(vis).resize((pw, int(pw * h / w)), Image.LANCZOS)
    p = DERIVED / f"{variants.tag(variant, 'digits-demo-' + timestr.replace(':', ''))}-{w}x{h}.png"
    prev.save(p)
    print(f"  wrote {p.relative_to(HERE)}  (demo '{timestr}', downscaled to {pw}w)")


if __name__ == "__main__":
    raise SystemExit(main())
