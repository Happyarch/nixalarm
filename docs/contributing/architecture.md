# Architecture

Goal: show how a run flows through the program and where each change belongs.

## Repo layout

- `src/`: all program code. `main` and scheduling live beside per-face renderers and per-domain helpers.
- `src/dial_*.cpp`, `src/astro.cpp`, `src/sundial.cpp`, `src/moondial.cpp`, `src/auto_dial.cpp`: sun and moon dial scene math and ray-traced faces.
- `src/analog.cpp`, `src/nixie.cpp`, `src/clock.cpp`: analog, nixie, and seven-segment faces plus face selection.
- `src/config.cpp`, `src/sdr_gain.cpp`, `src/source_setup.cpp`: config file, gain rules, wizard, and startup preflight.
- `src/alarm.cpp`, `src/audio.cpp`: alarm state machine and audio playback.
- `tests/`: one binary per C++ suite plus the Python CLI regressions in `tests/test_sdr_cli.py`.
- `assets/`: source masters, generators, and staged runtime art. Licensing record at [ATTRIBUTION](../../assets/ATTRIBUTION.md).
- `docs/`: user guides in `docs/users/`, man pages in `docs/man/`.
- `packaging/`: one directory per distributable format.
- `data/`: desktop file and installed icon.
- `third_party/`: vendored `stb_image.h` image decoder, included by the renderers.

## Data flow

```text
CLI args                        config file                    preflight
(`src/main.cpp`)                (`src/config.cpp`)             (`src/source_setup.cpp`)
     |                                |                                |
     v                                v                                v
 parse flags ---> `load_config` ---> `preflight_audio_sources` ---> SDL init
                                                                  (exit 2 on bad
                                                                  SDR gain first)
     |                                                                  |
     v                                                                  v
`make_clock_face`                                              `AlarmScheduler::poll`
(`src/clock.cpp`)                                              (`src/alarm.cpp`)
     |                                                                  |
     v                                                                  v
 face `render`                                              `AudioPlayer::start`
 (`src/clock.cpp`,                                          (`src/audio.cpp`)
  `src/nixie.cpp`,
  `src/analog.cpp`,
  `src/sundial.cpp`,
  `src/moondial.cpp`,
  `src/auto_dial.cpp`,
  `src/dial_gl.cpp`)
```

A run parses flags in `src/main.cpp`, loads the TOML-like config in `src/config.cpp` (merging built-in presets: offline `generated` plus the seven generic `weatherband_162_*` SDR presets), validates SDR gains in `src/source_setup.cpp` before SDL initializes, then selects the face once with `make_clock_face`. Only then does the loop start: each frame calls that face's `render`, while `AlarmScheduler::poll` in `src/alarm.cpp` decides when `AudioPlayer::start` in `src/audio.cpp` plays.

## Key entry points

`Config load_config(const fs::path& path, bool seed_if_missing = true)` in `src/config.cpp`
Takes a config path; returns the parsed config with theme applied and built-in presets merged. Writes the default file when missing unless `seed_if_missing` is false. Callers: `src/main.cpp` (seeding for normal runs), the `--configure-source` wizard path (non-seeding). Wizard and `--help` paths must stay side-effect free; see `tests/test_sdr_cli.py`.

`PreflightResult preflight_audio_sources(const Config& cfg, const std::string& primary_name, const std::string& fallback_name, bool audio_will_start, std::ostream& err, const PreflightContext& context = {})` in `src/source_setup.cpp`
Takes the config and the two source names; returns ok plus an exit code. Rejects an invalid SDR gain in the primary, or in the fallback when the primary is not `Generated`, with exit code 2 before SDL starts or an alarm arms, even though the runtime fallback to the generated tone would still sound. Skips all gain checks when `audio_will_start` is false (clock-only runs), and skips fallback validation for a `Generated` primary. Gain preflight is the only thing clock-only mode skips: `src/main.cpp` still rejects an unknown primary source name with exit code 2 in every mode. Missing `rtl_fm`, missing hardware, and network failures are not startup errors; they stay in the play-time path. Caller: `src/main.cpp`.

`bool validate_sdr_gain(const std::string& value, std::string* error = nullptr)` in `src/sdr_gain.cpp`
Takes raw config text; returns true for a finite decimal dB gain `rtl_fm` accepts as an explicit manual gain. Empty, `auto`, and non-numeric values fail. Callers: preflight and the wizard in `src/source_setup.cpp`.

`std::unique_ptr<ClockFace> make_clock_face(const Config& cfg)` in `src/clock.cpp`
Takes the config; returns the face matching `clock_face`, defaulting to seven-segment with a stderr note on unknown values. Faces: `SevenSegmentClock` in `src/clock.cpp`, `NixieClock` in `src/nixie.cpp`, `AnalogClock` in `src/analog.cpp`, dial faces in `src/sundial.cpp`, `src/moondial.cpp`, and `src/auto_dial.cpp` over the renderer in `src/dial_gl.cpp`. Caller: `src/main.cpp`.

`bool AlarmScheduler::poll()` in `src/alarm.cpp`
Takes no arguments; returns true the moment a scheduled alarm or elapsed snooze becomes due and starts ringing. Constructed with alarm times, snooze minutes, and hold-to-stop seconds. Caller: `src/main.cpp`, which then calls `AudioPlayer::start`.

`void AudioPlayer::start(Source source, const std::string& fallback, const std::map<std::string, Source>& all)` in `src/audio.cpp`
Takes the primary source, the fallback name, and all known sources; dispatches the primary by type on a worker thread. On failure it looks the fallback name up once and dispatches it by type, ignoring that result: a failed fallback has no second safety net. An unknown fallback name plays the generated tone directly. Callers: the alarm path and `--test-source` in `src/main.cpp`.

`void list_sources(const Config& cfg)` in `src/config.cpp`
Takes the config; prints each source name with a per-type detail suffix. A new source type needs a branch here or it prints without details. Caller: `src/main.cpp` for `--list-sources`.

## How to add things

### New theme

1. Add a branch to `apply_theme` in `src/config.cpp` setting `theme`, `clock_face`, and default colors, and extend the built-in theme listing in `default_config_text` in `src/config.cpp` so new installs see it.
2. If the look needs new rendering, add a `ClockFace` subclass (see `src/clock.h`) and select it in `make_clock_face` in `src/clock.cpp`. A recolor of an existing face needs no new class.
3. If step 2 adds source files, wire them into `CMakeLists.txt`: append to the matching static library (`dial_math`, `dial_render`, `analog_clock`, `source_setup`) or add a library and link it into `nixalarm`. An unwired file compiles nowhere.
4. Document the theme in [nixalarm(5)](../man/nixalarm.5) and the [appearance guide](../users/appearance.md).
5. Pin rendering behavior in the matching suite: `tests/test_analog.cpp` for the analog face, `tests/test_dial_*.cpp` for dial faces. Skipping this step leaves the theme covered only by manual inspection.

### New source type

1. Extend `SourceType` in `src/types.h` and parse its keys in `src/config.cpp`.
2. Dispatch it in `AudioPlayer::start` in `src/audio.cpp` in both the primary and the fallback branches. Wiring only the primary branch leaves the fallback path silently playing nothing for that type.
3. Add its detail suffix in `list_sources` in `src/config.cpp`, or `--list-sources` prints the name without details.
4. If the source needs startup validation, extend `preflight_audio_sources` in `src/source_setup.cpp`, following the SDR gain pattern in `src/sdr_gain.cpp`.
5. Add a generic built-in preset to `builtin_sources` in `src/config.cpp` only when it applies everywhere; location-specific transmitters, URLs, and coordinates stay in user configs.
6. Cover parsing and validation in `tests/test_source_setup.cpp` and `tests/test_sdr_gain.cpp`, and CLI behavior in `tests/test_sdr_cli.py`.

## Invariants

- The default install stays offline: `alarm_source` and `fallback_source` default to `generated` in `src/config.cpp`, and the shipped config comments keep every network and hardware example commented out.
- Startup validation and play-time failure are separate. Preflight rejects invalid SDR gains with exit code 2 before anything arms; hardware and network failures surface later, at play time in `src/audio.cpp`, which logs the failure and tries the configured fallback exactly once.
- A `Generated` primary never fails into the fallback: preflight leaves an unused fallback unvalidated and `AudioPlayer::start` plays the tone directly.
- The `--configure-source` wizard never writes files, initializes audio, touches hardware, or arms an alarm. Proposals that make it state-changing break `tests/test_sdr_cli.py`.
