#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd -- "$script_dir/../.." && pwd)
work_dir=$(mktemp -d /tmp/nixalarm-appimage.XXXXXX)
build_dir="${work_dir}/build"
appdir="${work_dir}/AppDir"
out_dir="${project_root}/dist"
trap 'rm -rf "$work_dir"' EXIT

cmake -S "$project_root" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$build_dir"

DESTDIR="$appdir" cmake --install "$build_dir"

cat > "$appdir/AppRun" <<'EOF'
#!/usr/bin/env sh
HERE=$(dirname "$(readlink -f "$0")")
export PATH="$HERE/usr/bin:$PATH"
exec "$HERE/usr/bin/nixalarm" "$@"
EOF
chmod +x "$appdir/AppRun"

cp "$project_root/data/nixalarm.desktop" "$appdir/nixalarm.desktop"
cp "$appdir/usr/share/icons/hicolor/scalable/apps/nixalarm.svg" "$appdir/nixalarm.svg"
cp "$appdir/usr/share/icons/hicolor/scalable/apps/nixalarm.svg" "$appdir/.DirIcon"

mkdir -p "$out_dir"
appimage_arch="${ARCH:-x86_64}"
ARCH="$appimage_arch" appimagetool "$appdir" "$out_dir/nixalarm-${appimage_arch}.AppImage"
