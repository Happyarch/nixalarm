# AGENTS.md

`nixalarm` is a small compiled Linux alarm utility. It renders configurable local-time clock displays (seven-segment, photoreal nixie tube, analog SVG, and ray-traced sundial/moondial) and plays an alarm source at a scheduled local `HH:MM`. The default alarm source is the offline `generated` tone.

## Tasks

Build and smoke-test from a clean checkout:

```sh
cmake -S . -B build
cmake --build build
./build/nixalarm --help
./build/nixalarm --list-sources
```

Full contributor setup, test commands, packaging, and asset workflows live in the [contributor guides](docs/contributing/build.md). The CLI contract is [nixalarm(1)](docs/man/nixalarm.1) and the config format is [nixalarm(5)](docs/man/nixalarm.5).

## Constraints

- Keep the CLI Unix-like: terse flags, useful stderr errors, no hidden background services.
- Prefer generated/rendered primitives over bundled image assets for the clock face.
- Keep visual styles config-driven. Current themes are `terminal_glow`, `sinnoh_green`, `nixie`, `analog`, `sundial`, `moondial` and `auto_dial`; add future styles as themes rather than replacing defaults.
- Keep the default alarm source reliable and offline: `generated`.
- Treat internet NOAA weather-radio mirrors as convenience audio sources, not emergency alerting.
- Ship nothing tied to a particular place. Built-in sources are the generic `weatherband_162_*` channel presets; a specific transmitter, station URL, or set of coordinates belongs in a user's own config, never in the defaults or the docs.

## Navigation

- [Build and test](docs/contributing/build.md): setup, configure, fast and full test runs.
- [Architecture](docs/contributing/architecture.md): data flow, entry points, recipes for a new theme or source.
- [Packaging](docs/contributing/packaging.md): distributable formats from the real scripts.
- [Assets](docs/contributing/assets.md): generator workflows and prerequisites.
- [User guides](docs/users/): task docs, including [alarms](docs/users/alarms.md), [sources](docs/users/sources.md), and [appearance](docs/users/appearance.md).
- [Asset attribution](assets/ATTRIBUTION.md): source, author, and license record. Preserve every entry.

Documentation policy, docs only: gitignored paths such as `build/` are generated output and are never fact sources for prose.

## Review checklist

- `cmake -S . -B build` and `cmake --build build` pass; on headless machines `--help` and `--list-sources` at least pass.
- No specific transmitter, station URL, or coordinates in defaults or docs.
- Default install stays on the offline `generated` source.
- New themes are config-driven additions, not replacements; new sources follow the recipe in [architecture](docs/contributing/architecture.md).
- Asset changes keep [attribution](assets/ATTRIBUTION.md) credits and license facts intact.
