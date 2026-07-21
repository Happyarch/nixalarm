# nixalarm

`nixalarm` is a small Linux alarm utility with a large green seven-segment clock display. It reads local time, rings at a requested local `HH:MM`, and supports local audio files, internet radio streams, optional RTL-SDR weather-band audio, and a built-in generated alarm tone.

The visual style is intentionally simple: dark green background, bright green segment blocks, dim inactive segments, and optional glow.

## Build

Dependencies:

- C++20 compiler
- CMake
- SDL2
- FFmpeg development libraries: `libavformat`, `libavcodec`, `libavutil`, `libswresample`
- Optional for SDR weather-band wake-up, only if you have SDR hardware: `rtl_fm`

Example:

```sh
cmake -S . -B build
cmake --build build
```

Arch package build:

```sh
cd packaging/arch
makepkg -f
```

Run:

```sh
./build/nixalarm 07:30
```

Manual pages are installed by CMake:

```sh
man nixalarm
man 5 nixalarm
```

## Usage

```text
nixalarm [OPTIONS] HH:MM

Options:
  --config PATH        Read config from PATH instead of XDG config.
  --source NAME        Use a configured or built-in source.
  --fullscreen         Start fullscreen.
  --windowed           Start windowed, overriding config fullscreen.
  --list-sources       Print built-in and configured sources.
  --test-source NAME   Play a source immediately.
  --help               Print help.
  --version            Print version.
```

Controls:

- Space tap while ringing: snooze.
- Space hold while ringing: dismiss after the configured hold duration.
- F11 or `f`: toggle fullscreen.
- `q` or Escape: quit.

## Config

Default path:

```text
$XDG_CONFIG_HOME/nixalarm/config.toml
```

Fallback:

```text
$HOME/.config/nixalarm/config.toml
```

Example:

```toml
alarm_source = "generated"
snooze_minutes = 10
hold_to_stop_seconds = 10
volume = 0.9
source_start_timeout_seconds = 8
fallback_source = "generated"

[window]
width = 800
height = 360
fullscreen = false
always_on_top = false

[style]
theme = "terminal_glow"
background = "#07180b"
segment_on = "#6cff57"
segment_off = "#123319"
glow = true
show_seconds = false

[sources.local_song]
type = "file"
path = "/home/user/Music/alarm.flac"

[sources.weather_stream]
type = "internet"
url = "https://example.org/your-stream"

[sources.weatherband]
type = "sdr_weatherband"
frequency_mhz = 162.500
device_index = 0
gain = "auto"
```

## Built-in sources

- `generated`: procedural alarm tone.
- `nwr_internet_backup`: practical no-SDR NOAA backup using the example NWR community mirror.
- `nwr_internet_backup`: NOAA Weather Radio an example, 162.450 MHz internet-stream preset when the community mirror is available.
- `weatherband_162_425`: future SDR alias for the preferred the local area-area NWR preset.
- `weatherband_162_425`: SDR preset for NOAA Weather Radio NWR, a regional area, 162.500 MHz, serves the local area.
- `weatherband_162_425`: alias for the same NWR SDR preset.
- `weatherband_162_400`, `weatherband_162_425`, `weatherband_162_450`, `weatherband_162_475`, `weatherband_162_500`, `weatherband_162_525`, `weatherband_162_550`: RTL-SDR weather-band presets.

Without SDR hardware, use example as the NOAA internet backup:

```sh
./build/nixalarm --source nwr_internet_backup 07:30
```

If you later add an RTL-SDR, use the preferred Jefferson-area NOAA station:

```sh
./build/nixalarm --source weatherband_162_425 07:30
```

Shorter SDR alias:

```sh
./build/nixalarm --source weatherband_162_425 07:30
```

The internet NOAA-weather-radio URLs are community mirrors when available. NWR is configured as SDR-first because public listings do not show a reliable online stream for that transmitter. Without SDR hardware, use the example stream backup. Do not rely on this program or those streams for emergency alerting.

## Themes

Set `theme` in `[style]` before any color overrides:

```toml
[style]
theme = "sinnoh_green"
show_seconds = false
```

Built-in themes:

- `terminal_glow`: original dark green background, bright green glowing digits.
- `sinnoh_green`: light green pixel-clock style with dark green block digits, matching the provided Pokémon DS-style reference more closely.

You can still override `background`, `segment_on`, `segment_off`, and `glow` after setting a theme.
