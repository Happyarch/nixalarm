#!/usr/bin/env python3
"""Assemble the flat runtime asset directory the NixieClock renderer loads.

The generators write into base/ and derived/ with descriptive names; the renderer
wants one directory with stable flat names.  This copies the pieces for a given
resolution into OUTDIR (default assets/runtime/nixie for hhmmss,
assets/runtime/nixie-<variant> otherwise):

    plate.png            <- base/nixie-clock/<plate-prefix>-<W>x<H>.png
    transmittance.png    <- derived/<transmittance-tag>-<W>x<H>.png
    static_emission.png  <- derived/<digits-tag>-<W>x<H>/static_emission.png
    digit_0..9.png       <- derived/<digits-tag>-<W>x<H>/digit_N.png
    meta.json            <- derived/<digits-tag>-<W>x<H>/meta.json

--variant selects the tube layout (default hhmmss; see nixie_variants.py).

Usage:
    python3 stage_assets.py WIDTH HEIGHT [OUTDIR] [--variant NAME]
    python3 stage_assets.py 3840 2160
"""
import sys
import shutil
from pathlib import Path

import nixie_variants as variants

HERE = Path(__file__).resolve().parent


def main() -> int:
    variant, argv = variants.pop_variant(sys.argv)
    if len(argv) < 3:
        print(__doc__)
        return 2
    w, h = int(argv[1]), int(argv[2])
    default_out = HERE / "runtime" / ("nixie" if variant == "hhmmss" else f"nixie-{variant}")
    out = Path(argv[3]) if len(argv) > 3 else default_out
    out.mkdir(parents=True, exist_ok=True)

    digits_dir = HERE / "derived" / f"{variants.tag(variant, 'digits')}-{w}x{h}"
    jobs = {
        "plate.png": variants.plate_path(variant, w, h),
        "backing.png": HERE / "derived" / f"{variants.tag(variant, 'backing')}-{w}x{h}.png",
        "transmittance.png": HERE / "derived" / f"{variants.tag(variant, 'transmittance')}-{w}x{h}.png",
        "static_emission.png": digits_dir / "static_emission.png",
        "meta.json": digits_dir / "meta.json",
    }
    for i in range(10):
        jobs[f"digit_{i}.png"] = digits_dir / f"digit_{i}.png"

    missing = [str(src) for src in jobs.values() if not src.exists()]
    if missing:
        print("missing inputs (run the generators first):")
        for m in missing:
            print("  " + m)
        return 1

    for name, src in jobs.items():
        shutil.copy2(src, out / name)
    print(f"staged {len(jobs)} files -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
