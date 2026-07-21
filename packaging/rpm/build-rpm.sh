#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd -- "$script_dir/../.." && pwd)
work_dir=$(mktemp -d /tmp/nixalarm-rpm.XXXXXX)
build_dir="${work_dir}/build"
out_dir="${project_root}/dist"
package_dir="${work_dir}/packages"
trap 'rm -rf "$work_dir"' EXIT

cmake -S "$project_root" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$build_dir"
mkdir -p "$out_dir"
mkdir -p "$package_dir"
(cd "$work_dir" && cpack --config "$build_dir/CPackConfig.cmake" -G RPM -B "$package_dir")
cp "$package_dir"/*.rpm "$out_dir"/
