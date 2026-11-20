# Assets

Goal: understand which artwork is source, which is regenerable, and how to rerun the real generators.

Prerequisites: Python 3 with Pillow and NumPy for the nixie scripts (see the imports in `assets/generate_digits.py`, `assets/generate_backing.py`, and `assets/generate_transmittance.py`). Icon vectorization needs Pillow, NumPy, and `potrace` on `PATH` (see `assets/icon/build_icon.py`). Dial map packing is recorded in [ATTRIBUTION](../../assets/ATTRIBUTION.md) as ImageMagick channel packing; no dial packing script ships in this repository.

## Layout

- `assets/base/` holds source masters and masks (the inputs).
- `assets/derived/` holds script output: crops, tints, glow passes, and sprite frames. It is gitignored regenerable output.
- `assets/runtime/nixie/` is the staged set the renderer loads and is tracked, because the `CMakeLists.txt` install rules ship it directly with no build step producing it. `assets/runtime/nixie-*/` variant staging directories are gitignored.
- `assets/runtime/analog/` and `assets/runtime/dial/` are tracked runtime inputs; `assets/runtime/icon.png` is the pre-rasterized window icon.

## Workflows

### Nixie tube sprites

```sh
python3 assets/generate_digits.py 3840 2160
python3 assets/generate_backing.py 3840 2160
python3 assets/generate_transmittance.py 3840 2160
python3 assets/stage_assets.py 3840 2160
```

The generators read `assets/base/` and write `assets/derived/`; `stage_assets.py` assembles one flat runtime directory from base and derived pieces for the requested resolution. Each generator accepts `--variant NAME` for alternate tube layouts, defaulting to `hhmmss`; the shared layout table lives in `assets/nixie_variants.py`. `generate_digits.py` also accepts `--demo` and `--plate` to composite a validation render, and `generate_transmittance.py` accepts `--preview` for the same purpose. Mask rescaling helpers are `assets/scale_masks_hhmmss_16x9.py` and `assets/scale_masks_hhmm_16x9.py`.

### App icon

`assets/icon/build_icon.py` vectorizes the flat-color source bitmaps in `assets/icon/source/` through per-color `potrace` passes into `data/icons/hicolor/scalable/apps/nixalarm.svg`. The script takes no arguments: input, per-layer working files, and output are constants at the top of the file (`SRC` and `OUT_SVG` point under `/tmp`), and it runs on import with no `main` guard. To rerun it, place the source bitmap at the `SRC` path (or edit the constants), create the working directory, run `python3 assets/icon/build_icon.py`, and install the resulting SVG at the `data/icons/` path. `assets/runtime/icon.png` is rasterized from that SVG for the SDL window icon.

### Dial maps

The sundial and moondial ground, plate, and gnomon maps in `assets/runtime/dial/` derive from CC0 packs by channel packing and format conversion. The exact base sources and packing are recorded in [ATTRIBUTION](../../assets/ATTRIBUTION.md). There is no dial generator script to rerun; to change these maps, repeat the documented packing by hand and keep the attribution entry accurate.

## Rules

- Record new base assets in [ATTRIBUTION](../../assets/ATTRIBUTION.md) with source, author, and license before staging them.
- Licensing questions resolve in [ATTRIBUTION](../../assets/ATTRIBUTION.md), never in this guide.

## Next

[ATTRIBUTION](../../assets/ATTRIBUTION.md) for every source, author, and license fact. The install rules in `CMakeLists.txt` show which runtime directories ship.
