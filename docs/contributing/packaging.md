# Packaging

Goal: produce a distributable `nixalarm` from the same CMake build used in development.

Prerequisites: the build dependencies from [build](build.md), plus only the tool of the target format. The Debian, RPM, and AppImage scripts pin `CFLAGS`/`CXXFLAGS` to an x86-64-v3 baseline (`-march=x86-64-v3 -mtune=generic -O2 -pipe`): their output requires that CPU level and does not run on older x86-64 hardware.

## Formats

### Debian and RPM via CPack

```sh
packaging/deb/build-deb.sh
packaging/rpm/build-rpm.sh
```

Each script configures a Release build in a temporary directory, compiles, runs `cpack -G DEB` or `cpack -G RPM` against the generated `CPackConfig.cmake`, and copies the result into `dist/`, which is gitignored generated output. The RPM path also requires `rpmbuild` on `PATH`. Package metadata (names, dependencies, contact) is set in the `CPack` block of `CMakeLists.txt`. Expected result: one `.deb` or `.rpm` in `dist/`.

### AppImage

```sh
packaging/appimage/build-appimage.sh
```

The script configures and installs a Release build into a temporary `AppDir`, writes an `AppRun` launcher, copies the desktop file from `data/nixalarm.desktop` and the icon from `data/icons/hicolor/scalable/apps/nixalarm.svg`, then runs `appimagetool` to emit `dist/nixalarm-<arch>.AppImage`. Requires `appimagetool` on `PATH`. The script assembles one bundle; it does not promise the bundle runs everywhere. Expected result: one `.AppImage` in `dist/`.

### Flatpak

```sh
packaging/flatpak/build-flatpak.sh
```

The script calls `flatpak-builder` with the manifest in `packaging/flatpak/io.github.Happyarch.nixalarm.yml`. Building needs the flatpak CLI plus the SDK and runtime the manifest names (`org.freedesktop.Sdk` and `org.freedesktop.Platform`, release 24.08). The build tree goes to `build-flatpak/`; the repository and the exported bundle go to `dist/flatpak-repo` and `dist/io.github.Happyarch.nixalarm.flatpak`. Expected result: the bundle in `dist/`.

### Arch

```sh
cd packaging/arch
makepkg -f
```

`packaging/arch/PKGBUILD` configures with `-DCMAKE_BUILD_TYPE=None`, builds, installs into the package directory, and installs `README.md` and `AGENTS.md` as documentation. Runtime and build dependencies are declared in the `depends`, `makedepends`, and `optdepends` arrays; `rtl-sdr` stays optional. Unlike the scripts above, this format does not write `dist/`: `makepkg` leaves the `.pkg.tar.zst` beside the `PKGBUILD`, and those artifacts are gitignored. Expected result: one package file in `packaging/arch/`.

### Nix

`packaging/nix/flake.nix` is a flake example pinned at version 0.2.1 for `x86_64-linux` and `aarch64-linux`, with a matching developer shell. It does not write `dist/`; the build product lives in the Nix store. Runnable from the repository root:

```sh
nix build ./packaging/nix
```

## If it fails

- `appimagetool: command not found`, `flatpak-builder: command not found`, or `rpmbuild: command not found` → install the format tool; none is vendored in this repository.
- A flatpak build fails on SDK or runtime errors → install the exact `org.freedesktop.Sdk` and `org.freedesktop.Platform` 24.08 refs the manifest names.
- Stale output in `dist/` → remove it and rerun; the scripts recreate `dist/` but never clean previous artifacts.
- Missing icons or man pages in the package → confirm the `CMakeLists.txt` install rules still cover `assets/runtime/`, `data/icons/`, and `docs/man/` before changing a script.

## Next

[Build](build.md) for the underlying CMake build, [architecture](architecture.md) for what gets installed and why. Installed file locations follow the `install()` rules in `CMakeLists.txt`.
