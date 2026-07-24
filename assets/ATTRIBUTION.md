# Asset attribution & licensing

Assets in this directory are **not** covered by the repository's top-level LICENSE.
Each base asset carries its own license, recorded below. Derivatives inherit the
license of the base they are generated from (a CC BY-SA base yields CC BY-SA
derivatives, etc.).

## Layout

- `base/`    — original source/generated assets (the inputs). One entry per file below.
- `derived/` — assets produced by scripting over `base/` (crops, tints, glow passes,
  off/on states, sprite frames). Note which base each was generated from.

## Base assets

| File | Source | License | Author / Credit | Notes |
|------|--------|---------|-----------------|-------|
| `assets/runtime/analog/clock_face.svg`, `assets/runtime/analog/clock_face_roman.svg` | Wikimedia Commons `Animated analog SVG clock.svg` | Public domain | Fleshgrinder | JavaScript instruction text, SMIL hand animation, and branding/credit text are not included; hand motion is implemented in C++. Roman variant swaps the Arabic numeral paths for SVG text labels. |
| `assets/runtime/dial/gnomon_diffuse.png`, `assets/runtime/dial/gnomon_normal.png`, `assets/runtime/dial/gnomon_params.png` | [3dtextures.me Metal 007](https://3dtextures.me/2025/01/20/metal-007/) | CC0 | João Paulo (3dtextures.me) — credit as courtesy | Sundial/moondial gnomon rod maps. diffuse = basecolor as-is; normal as-is; params = RGBA channel-pack of R=inverted roughness, G=ambient occlusion, B=height, A=metallic. Packed with ImageMagick from the 1024px pack. |
| `assets/runtime/dial/plate_diffuse.png`, `assets/runtime/dial/plate_normal.png`, `assets/runtime/dial/plate_params.png` | [3dtextures.me Cobblestone Irregular Floor 001](https://3dtextures.me/2025/12/23/cobblestone-irregular-floor-001/) | CC0 | João Paulo (3dtextures.me) — credit as courtesy | Sundial/moondial baseplate maps. diffuse = basecolor as-is; normal as-is; params = RGBA channel-pack of R=inverted roughness, G=ambient occlusion, B=height, A=black (dielectric). Packed with ImageMagick from the 1024px pack. |
| `assets/runtime/dial/ground_diffuse.png`, `assets/runtime/dial/ground_normal.png`, `assets/runtime/dial/ground_params.png` | [3dtextures.me Grass 003 (with flowers)](https://3dtextures.me/2018/05/01/grass-003-with-flowers/) | CC0 | João Paulo (3dtextures.me) — credit as courtesy | Sundial/moondial ground-plane maps. diffuse = COLOR as-is; normal = NRM; params = RGBA channel-pack of R=inverted ROUGH, G=OCC, B=DISP, A=black (dielectric). JPEG sources converted/resized to 1024px PNG with ImageMagick. |

### License quick-reference (for this project)

- **AI-generated** (GPT / Gemini output): treated as usable, no attribution owed.
  Still record the tool + prompt intent so derivatives are reproducible.
- **CC0 / Public Domain**: no attribution required (credit as courtesy).
- **CC BY 4.0**: attribution required (author + license + source URL). No share-alike.
- **CC BY-SA 3.0/4.0**: attribution required **and** derivatives must be redistributed
  under the same license. Keep such files clearly marked.
- **Not usable**: NC (NonCommercial) or ND (NoDerivatives).

## Derived assets

| File | Generated from (base) | How | Effective license |
|------|-----------------------|-----|-------------------|
| _(add rows as the generator produces them)_ | | | |
