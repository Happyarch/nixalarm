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
- Keep visual styles config-driven. Current themes are `terminal_glow`, `sinnoh_green`, `nixie`, `analog`, `sundial`, `moondial` and `auto_dial`; add future styles as themes rather than replacing defaults.
- Keep the default alarm source reliable and offline: `generated`.
- Treat internet NOAA weather-radio mirrors as convenience audio sources, not emergency alerting.
- Ship nothing tied to a particular place. Built-in sources are the generic `weatherband_162_*` channel presets; a specific transmitter, station URL, or set of coordinates belongs in a user's own config, never in the defaults or the docs.

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
