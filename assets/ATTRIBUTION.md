# Asset attribution and licensing

Assets in this directory are not covered by the repository's top-level LICENSE.
Each base asset carries its own license, recorded below. Derivatives inherit the
license of the base they are generated from (a CC BY-SA base yields CC BY-SA
derivatives, etc.).

The AI-generated rows are recorded as AI-generated / Permissive in the prior
project record; no specific license identifier is recorded for them. Credit
lines below are project record-keeping: keep them intact when editing.

## Layout

- `base/` holds original source and generated masters (the inputs). One entry per file below.
- `derived/` holds script output: crops, tints, glow passes, off and on states, and sprite frames. Each entry notes the base it was generated from.

## Base assets

| File | Source | License | Author and credit | Notes |
|------|--------|---------|-------------------|-------|
| `assets/runtime/analog/clock_face.svg`, `assets/runtime/analog/clock_face_roman.svg` | Wikimedia Commons `File:Animated analog SVG clock.svg` [^1] | Public domain dedication by the author [^1] | Fleshgrinder | JavaScript instruction text, SMIL hand animation, and branding text are not included; hand motion is implemented in C++. The Roman variant swaps the Arabic numeral paths for SVG text labels. |
| `assets/runtime/dial/gnomon_diffuse.png`, `assets/runtime/dial/gnomon_normal.png`, `assets/runtime/dial/gnomon_params.png` | 3dtextures.me `Metal 007` [^2] | CC0 [^2] | João Paulo (3dtextures.me), credited as courtesy | Sundial and moondial gnomon rod maps. diffuse is basecolor as-is; normal as-is; params is an RGBA channel pack of R as inverted roughness, G as ambient occlusion, B as height, A as metallic. Packed with ImageMagick from the 1024px pack. |
| `assets/runtime/dial/plate_diffuse.png`, `assets/runtime/dial/plate_normal.png`, `assets/runtime/dial/plate_params.png` | 3dtextures.me `Cobblestone Irregular Floor 001` [^3] | CC0 [^3] | João Paulo (3dtextures.me), credited as courtesy | Sundial and moondial baseplate maps. diffuse is basecolor as-is; normal as-is; params is an RGBA channel pack of R as inverted roughness, G as ambient occlusion, B as height, A as black (dielectric). Packed with ImageMagick from the 1024px pack. |
| `assets/runtime/dial/ground_diffuse.png`, `assets/runtime/dial/ground_normal.png`, `assets/runtime/dial/ground_params.png` | 3dtextures.me `Grass 003 (with flowers)` [^4] | CC0 [^4] | João Paulo (3dtextures.me), credited as courtesy | Sundial and moondial ground-plane maps. diffuse is COLOR as-is; normal is NRM; params is an RGBA channel pack of R as inverted ROUGH, G as OCC, B as DISP, A as black (dielectric). JPEG sources converted and resized to 1024px PNG with ImageMagick. |
| `assets/base/nixie-clock/*` | Midjourney, AI generation | AI-generated | Happyarch (prompt author) | Photoreal nixie clock base plate renders (HH:MM and HH:MM:SS) and isolation masks for bulb and tube regions. |
| `assets/icon/source/nixalarm-logo.png`, `assets/icon/source/nixalarm-logo-non-indexed.png` | ChatGPT, DALL-E 3 | AI-generated | Happyarch (prompt author) | Source flat-color bitmap clock logo used for vectorization. |

### License quick-reference (acceptance policy)

- `AI-generated`: recorded as AI-generated / Permissive; no specific license identifier recorded. Still record the tool and prompt intent so derivatives are reproducible.
- `CC0` / public domain: no permission needed (credit as courtesy where recorded). [^5]
- `CC BY 4.0`: usable with attribution (author, license, source URL). [^6]
- `CC BY-SA 3.0/4.0`: usable with attribution, and derivatives carry the same license. [^7]
- Not usable: `NC` (NonCommercial) or `ND` (NoDerivatives).

## Derived assets

| File | Generated from (base) | How | Effective license |
|------|-----------------------|-----|-------------------|
| `assets/runtime/nixie/*` (`plate.png`, `backing.png`, `transmittance.png`, `static_emission.png`, `digit_0..9.png`, `meta.json`) | `assets/base/nixie-clock/*` | Composited and sliced via `generate_backing.py`, `generate_transmittance.py`, `generate_digits.py`, and staged via `stage_assets.py`. | AI-generated / Permissive |
| `data/icons/hicolor/scalable/apps/nixalarm.svg` | `assets/icon/source/nixalarm-logo-non-indexed.png` | Vectorized with per-color potrace passes via `assets/icon/build_icon.py`. | AI-generated / Permissive |
| `assets/runtime/icon.png` | `data/icons/hicolor/scalable/apps/nixalarm.svg` | Rasterized for the SDL window and taskbar icon. | AI-generated / Permissive |
| `assets/runtime/analog/*` | Wikimedia Commons `File:Animated analog SVG clock.svg` | Stripped branding and SMIL animation; added Roman numeral variant. | Public domain |
| `assets/runtime/dial/*` | 3dtextures.me CC0 packs | ImageMagick channel packing and format conversion. | CC0 |

## References

[^1]: Fleshgrinder, `File:Animated analog SVG clock.svg`, Wikimedia Commons, 2010. File page: https://commons.wikimedia.org/wiki/File:Animated_analog_SVG_clock.svg . Original file: https://upload.wikimedia.org/wikipedia/commons/4/4d/Animated_analog_SVG_clock.svg . The file page records the work as released into the public domain by its author.
[^2]: 3dtextures.me, `Metal 007`, 2025. Page: https://3dtextures.me/2025/01/20/metal-007/ . The page tags the texture `cc0`.
[^3]: 3dtextures.me, `Cobblestone Irregular Floor 001`, 2025. Page: https://3dtextures.me/2025/12/23/cobblestone-irregular-floor-001/ . The page tags the texture `cc0`.
[^4]: 3dtextures.me, `Grass 003 with flowers`, 2018. Page: https://3dtextures.me/2018/05/01/grass-003-with-flowers/ . The page titles it a CC0 seamless texture.
[^5]: Creative Commons, CC0 1.0 Universal public domain dedication. https://creativecommons.org/publicdomain/zero/1.0/
[^6]: Creative Commons, Attribution 4.0 International license. https://creativecommons.org/licenses/by/4.0/
[^7]: Creative Commons, Attribution-ShareAlike 3.0 Unported and Attribution-ShareAlike 4.0 International licenses (accessed 2026). https://creativecommons.org/licenses/by-sa/3.0/ and https://creativecommons.org/licenses/by-sa/4.0/
