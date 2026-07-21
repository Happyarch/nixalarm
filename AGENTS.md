# AGENTS.md

## Project

`nixalarm` is a small compiled Linux alarm utility. It renders a large green seven-segment local-time display and plays an alarm source at a scheduled local `HH:MM`.

## Build

```sh
cmake -S . -B build
cmake --build build
```

Required dependencies:

- C++20 compiler
- CMake
- SDL2
- FFmpeg development libraries: `libavformat`, `libavcodec`, `libavutil`, `libswresample`

Optional runtime dependency, only when SDR hardware is available:

- `rtl_fm` for RTL-SDR NOAA weather-band sources

## Run

```sh
./build/nixalarm 07:30
./build/nixalarm --list-sources
./build/nixalarm --test-source generated
```

## Conventions

- Keep the CLI Unix-like: terse flags, useful stderr errors, no hidden background services.
- Prefer generated/rendered primitives over bundled image assets for the clock face.
- Keep visual styles config-driven. Current themes are `terminal_glow` and `sinnoh_green`; add future styles such as nixie as themes rather than replacing defaults.
- Keep the default alarm source reliable and offline: `generated`.
- Treat internet NOAA weather-radio mirrors as convenience audio sources, not emergency alerting.
- Preserve the Caldwell County/Lenoir presets:
  - `nwr_caldwell_internet_backup` for current no-SDR use.
  - `nwr_nc_black_mountain_wng588`
  - `nwr_nc_mount_jefferson_wng588`
  - `nwr_caldwell_preferred` for future SDR use.
  - `nwr_nc_linville_wng538`

## Validation

Before handing off changes, run:

```sh
cmake -S . -B build
cmake --build build
```

If SDL video/audio cannot run in the environment, at least validate:

```sh
./build/nixalarm --help
./build/nixalarm --list-sources
```
