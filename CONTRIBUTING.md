# Contributing

Prerequisites: a Linux machine with a C++20 compiler, CMake 3.20 or newer, pkg-config, and the libraries listed in the [build guide](docs/contributing/build.md).

Start here:

- [Build and test](docs/contributing/build.md) for setup, the fast and full test runs, and headless validation.
- [Architecture](docs/contributing/architecture.md) for the program structure and the recipes for adding a theme or a source.
- [Packaging](docs/contributing/packaging.md) for distributable formats.
- [Assets](docs/contributing/assets.md) for regenerating artwork and icons.

Project rules are identical for agents and humans and live in [AGENTS.md](AGENTS.md): keep the default source offline, keep location-specific data out of defaults and docs, and run the review checklist before handing off changes.

Asset licensing and source provenance are recorded in [assets/ATTRIBUTION.md](assets/ATTRIBUTION.md). Keep every credit and license fact intact when touching `assets/`.
