#!/usr/bin/env python3
"""Generate the nixie transmittance map: a static grayscale layer encoding how much
light escapes each pixel, used at runtime as

    visible = base_plate + emission * transmittance      (additive, over black)

Transmittance = glass_attenuation * mesh_occlusion, confined to the tube interiors
(from the masks), with AIR (outside the tubes) left at 1.0 so the digit glow spills
past the glass onto the bezel.

Both effects are computed mathematically, no external texture:
  * glass    - a cylindrical horizontal falloff (sqrt(1-u^2)) so light dims toward
               each tube's curved rim.
  * mesh     - the honeycomb anode grid, as the Voronoi edges of a triangular
               lattice of hex centers, horizontally compressed toward the rim
               (asin remap) to read as wire wrapped around a cylinder.

Usage:
    python3 generate_transmittance.py WIDTH HEIGHT [--variant NAME] [--preview PLATE.png]

--variant selects the tube layout (default hhmmss; see nixie_variants.py). Writes
derived/transmittance-<W>x<H>.png for hhmmss, derived/transmittance-<variant>-<W>x<H>.png
otherwise. With --preview, also writes the matching transmittance-preview file,
simulating every tube glowing so the mesh/glass/spill interaction is visible over the
real plate.
"""
import sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageFilter

import nixie_variants as variants

HERE = Path(__file__).resolve().parent
DERIVED = HERE / "derived"

SRC_MASKS = None   # set in main() once --variant is parsed
TUBE_MASKS = None  # ditto

# --- material parameters (tuned at the 3840x2160 master; scale with resolution) ---
P = dict(
    glass_transmit=0.92,   # peak transmittance through the clear glass
    glass_rim_floor=0.32,  # transmittance at the extreme cylindrical rim
    hex_radius=18.0,       # hex circumradius in px @ master (cell size)
    hex_wire=3.2,          # wire thickness in px @ master
    hex_compress=0.97,     # asin edge-compression clamp (0..1); higher = tighter rim
    mesh_min=0.16,         # transmittance on a mesh wire (occlusion); lower = mesh reads darker/clearer over the glow
)
MASTER_H = 2160.0  # parameters above are expressed at this height; scale others to it


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def load_mask_alpha(name, w, h):
    """Return the mask's alpha coverage resized to (h, w) as float 0..1."""
    img = Image.open(SRC_MASKS / name).convert("RGBA").resize((w, h), Image.LANCZOS)
    return np.asarray(img)[:, :, 3].astype(np.float32) / 255.0


def bbox_of(alpha):
    ys, xs = np.where(alpha > 0.02)
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def hex_edge_strength(xh, yh, R, wire):
    """Voronoi-edge (honeycomb) strength of a triangular hex lattice, 0..1.

    A pixel is on a mesh wire when it is roughly equidistant from its two nearest
    hex centers, i.e. |d2 - d1| < wire.  Pointy-top lattice: rows step by 1.5R and
    alternate a half-cell horizontal offset.
    """
    dx = np.sqrt(3.0) * R
    dy = 1.5 * R
    r0 = np.round(yh / dy).astype(np.int32)
    d1 = np.full(xh.shape, np.inf, np.float32)
    d2 = np.full(xh.shape, np.inf, np.float32)
    for dr in (-1, 0, 1):
        r = r0 + dr
        yc = r * dy
        off = (np.mod(r, 2)) * (dx / 2.0)
        c0 = np.round((xh - off) / dx).astype(np.int32)
        for dc in (-1, 0, 1):
            xcc = (c0 + dc) * dx + off
            dist = np.hypot(xh - xcc, yh - yc).astype(np.float32)
            lt1 = dist < d1
            d2 = np.where(lt1, d1, np.minimum(d2, dist))
            d1 = np.where(lt1, dist, d1)
    # wire where the two nearest centers are ~equidistant
    return 1.0 - smoothstep(0.0, wire, d2 - d1)


def tube_transmittance(alpha, w, h, scale, mesh_scale):
    """Compute per-tube transmittance within its bbox; return full-canvas arrays
    (t_tube, coverage) so the caller can composite over air."""
    x0, y0, x1, y1 = bbox_of(alpha)
    bw, bh = x1 - x0, y1 - y0
    ys, xs = np.mgrid[0:bh, 0:bw].astype(np.float32)

    # local normalised horizontal coord u in [-1, 1] across the tube
    u = (2.0 * xs / max(bw - 1, 1)) - 1.0

    # cylindrical glass falloff: bright center, dim rim
    gc = np.sqrt(np.clip(1.0 - u * u, 0.0, 1.0))
    glass = P["glass_transmit"] * (P["glass_rim_floor"] + (1 - P["glass_rim_floor"]) * gc)

    # honeycomb, compressed toward the rim via asin remap of the horizontal coord
    uc = np.clip(u, -P["hex_compress"], P["hex_compress"])
    x_hex = (np.arcsin(uc) * (2.0 / np.pi) + 1.0) * 0.5 * bw
    edge = hex_edge_strength(x_hex, ys, P["hex_radius"] * scale * mesh_scale, P["hex_wire"] * scale * mesh_scale)
    mesh = 1.0 - (1.0 - P["mesh_min"]) * edge

    t_local = (glass * mesh).astype(np.float32)

    t_tube = np.zeros((h, w), np.float32)
    t_tube[y0:y1, x0:x1] = t_local
    return t_tube


def main():
    global SRC_MASKS, TUBE_MASKS
    variant, argv = variants.pop_variant(sys.argv)
    if len(argv) < 3:
        print(__doc__)
        return 2
    w, h = int(argv[1]), int(argv[2])
    scale = h / MASTER_H
    mesh_scale = variants.mesh_scale(variant)
    SRC_MASKS = variants.masks_dir(variant)
    TUBE_MASKS = variants.tube_masks(variant)
    DERIVED.mkdir(exist_ok=True)

    transmittance = np.ones((h, w), np.float32)  # air = 1.0 everywhere
    for name in TUBE_MASKS:
        a = load_mask_alpha(name, w, h)
        t_tube = tube_transmittance(a, w, h, scale, mesh_scale)
        # composite the tube's transmittance in, feathered by mask coverage
        transmittance = transmittance * (1.0 - a) + t_tube * a

    out = (np.clip(transmittance, 0, 1) * 255).astype(np.uint8)
    out_path = DERIVED / f"{variants.tag(variant, 'transmittance')}-{w}x{h}.png"
    Image.fromarray(out, "L").save(out_path)
    print(f"wrote {out_path.relative_to(HERE)}  ({w}x{h})")

    if "--preview" in argv:
        plate_path = Path(argv[argv.index("--preview") + 1])
        preview(transmittance, plate_path, w, h, variant)


def preview(transmittance, plate_path, w, h, variant):
    """Simulate every tube fully lit to eyeball mesh/glass/spill over the plate."""
    plate = Image.open(plate_path).convert("RGB").resize((w, h), Image.LANCZOS)
    plate_a = np.asarray(plate).astype(np.float32)

    # crude emission: warm orange fill inside every tube, blurred for bloom + spill
    coverage = np.zeros((h, w), np.float32)
    for name in TUBE_MASKS:
        coverage = np.maximum(coverage, load_mask_alpha(name, w, h))
    orange = np.array([255, 106, 0], np.float32)
    emis = coverage[:, :, None] * orange[None, None, :]
    emis_img = Image.fromarray(np.clip(emis, 0, 255).astype(np.uint8))
    bloom = np.asarray(emis_img.filter(ImageFilter.GaussianBlur(h * 0.010))).astype(np.float32)
    spill = np.asarray(emis_img.filter(ImageFilter.GaussianBlur(h * 0.030))).astype(np.float32)
    emission = 0.9 * bloom + 0.5 * spill

    vis = plate_a + emission * transmittance[:, :, None]
    vis = np.clip(vis, 0, 255).astype(np.uint8)
    pw = 1920
    prev = Image.fromarray(vis).resize((pw, int(pw * h / w)), Image.LANCZOS)
    p = DERIVED / f"{variants.tag(variant, 'transmittance-preview')}-{w}x{h}.png"
    prev.save(p)
    print(f"wrote {p.relative_to(HERE)}  (preview, downscaled to {pw}w)")


if __name__ == "__main__":
    raise SystemExit(main())
