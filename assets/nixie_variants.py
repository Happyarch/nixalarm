"""Shared per-layout config for the nixie asset generator scripts.

A "variant" is a tube layout: which mask set to load, how many digit tubes and
colon-bulb pairs it has, what the base plate is named, and a mesh-density
correction for variants whose tubes render at a different physical size than
the original hhmmss master (see mesh_scale below).

"hhmmss" is the default everywhere so its derived/runtime filenames stay
exactly as they were before variants existed (derived/backing-<W>x<H>.png,
etc.). Any other variant's derived outputs get a "-<variant>" tag so the two
sets can live side by side in derived/ without colliding.
"""
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASE = HERE / "base" / "nixie-clock"

VARIANTS = {
    "hhmmss": dict(
        mask_tag="HH:MM:SS-16:9",
        plate_prefix="HH:MM:SS-16:9",
        tube_count=6,
        bulb_count=2,
        mesh_scale=1.0,
    ),
    "hhmm": dict(
        mask_tag="HH:MM-16:9",
        plate_prefix="HH:MM-16:9",
        tube_count=4,
        bulb_count=1,
        # hhmm tubes render ~6.5% wider than the hhmmss master (180px vs
        # 169px bbox width @ the 1672x941 mask canvas). hex_radius/hex_wire in
        # generate_transmittance.py are absolute px, not tube-relative, so
        # without this the honeycomb reads slightly finer/denser on hhmm than
        # it does on hhmmss. Scale it back up to match.
        mesh_scale=180.0 / 169.0,
    ),
}


def get(variant):
    try:
        return VARIANTS[variant]
    except KeyError:
        raise SystemExit(f"unknown variant {variant!r}; choices: {', '.join(VARIANTS)}")


def masks_dir(variant):
    return BASE / f"masks-{get(variant)['mask_tag']}"


def tube_masks(variant):
    v = get(variant)
    return [f"nixie-tube-{i}-mask-{v['mask_tag']}.png" for i in range(1, v["tube_count"] + 1)]


def bulb_masks(variant):
    v = get(variant)
    return [f"bulb-{i}-mask-{v['mask_tag']}.png" for i in range(1, v["bulb_count"] + 1)]


def plate_path(variant, w, h):
    return BASE / f"{get(variant)['plate_prefix']}-{w}x{h}.png"


def mesh_scale(variant):
    return get(variant)["mesh_scale"]


def tag(variant, stem):
    """Derived-output filename stem, tagged with the variant unless it's the
    default hhmmss (whose filenames predate variants and stay bare)."""
    return stem if variant == "hhmmss" else f"{stem}-{variant}"


def scale_masks(variant, w, h):
    """Lanczos-resize a variant's source masks into masks_dir/<W>x<H>/, preserving
    RGBA verbatim (just resampled). Shared by the per-variant scale_masks_*.py
    entry points so the resize logic itself isn't duplicated per variant."""
    from PIL import Image

    src = masks_dir(variant)
    out_dir = src / f"{w}x{h}"
    out_dir.mkdir(parents=True, exist_ok=True)

    masks = sorted(src.glob("*.png"))
    if not masks:
        print(f"no masks found in {src}")
        return 1

    for p in masks:
        img = Image.open(p).convert("RGBA")
        src_ar = img.width / img.height
        dst_ar = w / h
        if abs(src_ar - dst_ar) / src_ar > 0.005:
            print(f"  ! {p.name}: aspect {src_ar:.4f} -> {dst_ar:.4f} (will distort)")
        out = img.resize((w, h), Image.LANCZOS)
        out.save(out_dir / p.name)
        print(f"  {p.name} -> {out_dir.relative_to(HERE)}/{p.name}  ({w}x{h})")

    print(f"done: {len(masks)} masks -> {out_dir.relative_to(HERE)}")
    return 0


def pop_variant(argv):
    """Pull --variant NAME out of argv in place; return the variant name
    (default 'hhmmss') and the remaining args."""
    variant = "hhmmss"
    argv = list(argv)
    if "--variant" in argv:
        i = argv.index("--variant")
        variant = argv[i + 1]
        del argv[i:i + 2]
    get(variant)  # validate
    return variant, argv
