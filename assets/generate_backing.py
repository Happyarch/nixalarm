#!/usr/bin/env python3
"""Generate the nixie tube BACKING layer.

A real nixie tube is not see-through: behind the digit stack and the anode mesh is
an opaque cathode backing plate/foil, curved to the tube's cylinder. Even when a
tube is off you see this dark warm-grey backing with the honeycomb silhouetted
against it -- never the board behind. The base plate photo leaves the tube
interiors empty (effectively transparent to black), so we synthesize the backing
as the backmost tube layer. Physical draw order is backing -> digits -> mesh ->
glass, so the compositor occludes the SUM (backing + digit emission) with the
single transmittance map (mesh * glass) in one pass:

    visible = plate + transmittance * (backing + emission)

The backing therefore carries NO mesh of its own -- the transmittance layer
supplies the honeycomb (and glass falloff) in front of BOTH the backing and the
digits, which is exactly why the mesh is still visible on an OFF tube (backing is
darkened by the mesh via transmittance).

The backing is per-tube: a cylindrical dark shading (bright-ish vertical center
falling to dark rims, sqrt(1-u^2)) in warm metallic grey, plus a soft specular
sheen (metal foil). rgb is premultiplied by the (boosted, feathered) mask coverage
so the compositor can add it straight over the black tube interior.

Usage:
    python3 generate_backing.py WIDTH HEIGHT [--variant NAME]

--variant selects the tube layout (default hhmmss; see nixie_variants.py). Writes
derived/backing-<W>x<H>.png for hhmmss, derived/backing-<variant>-<W>x<H>.png
otherwise (RGBA; premultiplied rgb, alpha = coverage).
"""
import sys
from pathlib import Path
import numpy as np
from PIL import Image

import nixie_variants as variants
# reuse the mask helpers (the mesh itself now lives only in the transmittance layer)
import generate_transmittance as gt
from generate_transmittance import bbox_of, MASTER_H, DERIVED

# --- backing material (expressed at the 3840x2160 master; scaled by H/2160) ---
B = dict(
    center=(26, 22, 18),   # warm dark grey at the cylinder's vertical center
    rim=(7, 6, 5),         # near-black at the curved rim
    rim_floor=0.30,        # shading floor at the extreme rim
    alpha_boost=1.45,      # multiply mask coverage so the interior reads opaque
    # the backing is metal foil -> a soft specular sheen where light catches the
    # curved plate: a vertical highlight band, slightly off-centre, fading at top/bottom.
    spec_color=(34, 31, 27),  # near-neutral bright catch added at the highlight peak
    spec_u0=-0.16,         # highlight horizontal position (-1..1); slightly left
    spec_width=0.26,       # highlight band width (in u)
    spec_vfade=0.35,       # how much the streak fades toward the top/bottom
)


def tube_backing(alpha, w, h, scale, rgb, a_out):
    x0, y0, x1, y1 = bbox_of(alpha)
    bw, bh = x1 - x0, y1 - y0
    ys, xs = np.mgrid[0:bh, 0:bw].astype(np.float32)
    u = (2.0 * xs / max(bw - 1, 1)) - 1.0

    # cylindrical shading: bright center strip -> dark rim
    gc = np.sqrt(np.clip(1.0 - u * u, 0.0, 1.0))
    shade = B["rim_floor"] + (1.0 - B["rim_floor"]) * gc  # 0..1

    center = np.array(B["center"], np.float32)
    rim = np.array(B["rim"], np.float32)
    base = rim[None, None, :] + (center - rim)[None, None, :] * shade[:, :, None]

    # specular sheen of the metal foil: an off-centre vertical highlight band that
    # fades toward the ends of the plate (reads as a light source catching the curve)
    vy = ys / max(bh - 1, 1)                       # 0..1 top->bottom
    vfade = 1.0 - B["spec_vfade"] * np.abs(2.0 * vy - 1.0)
    spec = np.exp(-((u - B["spec_u0"]) ** 2) / (2.0 * B["spec_width"] ** 2)) * vfade
    base += np.array(B["spec_color"], np.float32)[None, None, :] * spec[:, :, None]

    a = np.clip(alpha[y0:y1, x0:x1] * B["alpha_boost"], 0.0, 1.0)
    # composite this tube into the full-canvas accumulators (alpha-over)
    rgb[y0:y1, x0:x1, :] = rgb[y0:y1, x0:x1, :] * (1 - a[:, :, None]) + base * a[:, :, None]
    a_out[y0:y1, x0:x1] = np.maximum(a_out[y0:y1, x0:x1], a)


def main():
    variant, argv = variants.pop_variant(sys.argv)
    if len(argv) < 3:
        print(__doc__)
        return 2
    w, h = int(argv[1]), int(argv[2])
    scale = h / MASTER_H
    gt.SRC_MASKS = variants.masks_dir(variant)
    tube_masks = variants.tube_masks(variant)

    rgb = np.zeros((h, w, 3), np.float32)
    a_out = np.zeros((h, w), np.float32)
    for name in tube_masks:
        tube_backing(gt.load_mask_alpha(name, w, h), w, h, scale, rgb, a_out)

    out = np.dstack([np.clip(rgb, 0, 255), np.clip(a_out * 255, 0, 255)]).astype(np.uint8)
    DERIVED.mkdir(exist_ok=True)
    p = DERIVED / f"{variants.tag(variant, 'backing')}-{w}x{h}.png"
    Image.fromarray(out, "RGBA").save(p)
    print(f"wrote {p.relative_to(p.parent.parent)}  ({w}x{h})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
