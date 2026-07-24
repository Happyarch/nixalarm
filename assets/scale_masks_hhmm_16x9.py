#!/usr/bin/env python3
"""Scale the nixie-clock HH:MM 16:9 masks to a target resolution with a plain
high-quality filter (Lanczos). Masks are clean geometry, so no AI upscaler is
wanted here -- the photographic base plate is the only thing that goes through
Upscayl.

This mask set is specific to the 4-tube HH:MM 16:9 layout; see
scale_masks_hhmmss_16x9.py for the 6-tube HH:MM:SS layout. Both are thin
wrappers around nixie_variants.scale_masks() so the resize logic itself lives
in one place, but each is named for its variant so there's no flag to get
wrong.

Usage:
    python3 scale_masks_hhmm_16x9.py WIDTH HEIGHT
    python3 scale_masks_hhmm_16x9.py 1920 1080

Outputs into  base/nixie-clock/masks-HH:MM-16:9/<WIDTH>x<HEIGHT>/  next to the
source masks, preserving RGBA (the red draw color + feathered alpha are kept
verbatim, just resampled). The source masks are the 1672x941 canvas set.
"""
import sys

import nixie_variants as variants


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    w, h = int(sys.argv[1]), int(sys.argv[2])
    return variants.scale_masks("hhmm", w, h)


if __name__ == "__main__":
    raise SystemExit(main())
