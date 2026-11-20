# Build and test

Goal: configure, compile, and exercise `nixalarm` from a clean checkout.

Prerequisites, split by phase:

- Build: `git`, a C++20 compiler, CMake 3.20 or newer (`cmake_minimum_required` in `CMakeLists.txt`), and pkg-config itself (`find_package(PkgConfig REQUIRED)` fails configure without it), plus development files for SDL2, FFmpeg (`libavformat`, `libavcodec`, `libavutil`, `libswresample`), FluidSynth, `librsvg-2.0`, and `epoxy`. The libraries are probed through pkg-config, so a missing item fails at configure time with the module name. Python 3 is optional and only enables the CLI regression suite.
- Run: a Linux display server for the window and an audio device (the SDL dummy driver suffices for tests). Headless machines can only run the non-video checks below.
- Optional at runtime, only with SDR hardware: `rtl_fm` for RTL-SDR radio sources.

## Steps

1. Clone and enter the tree.

   ```sh
   git clone https://github.com/Happyarch/nixalarm.git
   cd nixalarm
   ```

   Expected: a working checkout; the remote matches the project homepage recorded in `CMakeLists.txt` and `packaging/arch/PKGBUILD`.

2. Configure and build.

   ```sh
   cmake -S . -B build
   cmake --build build
   ```

   Expected: the build completes and `build/nixalarm` exists. `build/` is gitignored generated output, not source.

3. Quick check: compiled suites only.

   ```sh
   ctest --test-dir build --output-on-failure -E sdr_cli
   ```

   Expected: `sdr_gain_tests`, `source_setup_tests`, `dial_geometry_tests`, `dial_scene_tests`, `dial_engrave_tests`, `dial_time_tests`, and `analog_tests` pass.

4. Full check: everything, including the Python CLI suite.

   ```sh
   ctest --test-dir build --output-on-failure
   ```

   Expected: all suites pass. `sdr_cli_tests` registers only when a Python 3 interpreter is found (`CMakeLists.txt` guards it with `find_package(Python3 ... QUIET)`); its fake-receiver case skips itself without `xvfb-run`.

5. Smoke-test without touching the real user config.

   ```sh
   (
     cfg_home="$(mktemp -d)" || exit 1
     trap 'rm -rf "$cfg_home"' EXIT
     XDG_CONFIG_HOME="$cfg_home" ./build/nixalarm --help
     XDG_CONFIG_HOME="$cfg_home" ./build/nixalarm --list-sources
   )
   ```

   Expected: exit code 0. Isolation matters: `--list-sources` loads the config with seeding enabled, so it writes the default config file when none exists. The snippet runs entirely inside a subshell, so the throwaway directory lives in a subshell-local variable with trap cleanup: the caller's environment and variables are untouched and only the mktemp directory is removed. `--help` and `--version` never create or modify a config.

6. Optional: install to a user prefix.

   ```sh
   cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local"
   cmake --build build
   cmake --install build
   ```

   Expected: the binary lands at `$HOME/.local/bin/nixalarm`, runtime art under `$HOME/.local/share/nixalarm`, and the man pages under `$HOME/.local/share/man/man1` and `man5`. The prefix is baked into the binary as its asset directory at configure time (`NIXALARM_DATADIR` in `CMakeLists.txt`), with an executable-relative fallback, so the installed binary finds its art without the source tree and nothing is regenerated. No elevated privileges are involved. Add `$HOME/.local/bin` to `PATH` to run it; `man` finds the pages once the prefix `share/man` directory is on the man search path.

## If it fails

- Missing `pkg_check_modules` entry or `Could not find PkgConfig` → install pkg-config and the matching development package, then re-run step 2.
- `sdr_cli_tests` absent from the full run → no Python 3 interpreter was found; expected, not an error.
- The fake-receiver case reports a skip → `xvfb-run` is not installed; install `xvfb` tooling to run it.
- `SDL init failed` on a headless machine → expected without a display server; the step 5 checks are the supported validation there.

## Next

[Architecture](architecture.md) for where a change belongs, [packaging](packaging.md) for distributable formats. The CLI surface is [nixalarm(1)](../man/nixalarm.1) and the config format is [nixalarm(5)](../man/nixalarm.5).
