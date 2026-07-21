#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd -- "$script_dir/../.." && pwd)
manifest="$script_dir/io.github.Happyarch.nixalarm.yml"
build_dir="$project_root/build-flatpak"
repo_dir="$project_root/dist/flatpak-repo"
bundle="$project_root/dist/io.github.Happyarch.nixalarm.flatpak"

command -v flatpak-builder >/dev/null

mkdir -p "$project_root/dist"
flatpak-builder --force-clean --repo="$repo_dir" "$build_dir" "$manifest"
flatpak build-bundle "$repo_dir" "$bundle" io.github.Happyarch.nixalarm
