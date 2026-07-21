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
icon_src=$(
  find /usr/share/icons -name 'preferences-system-time.png' -o -name 'preferences-system-time.svg' |
    sort -Vr |
    sed -n '1p'
)
if [[ -n "$icon_src" ]]; then
  icon_ext="${icon_src##*.}"
  cp "$icon_src" "$appdir/preferences-system-time.${icon_ext}"
  cp "$icon_src" "$appdir/.DirIcon"
else
  echo "nixalarm: warning: preferences-system-time icon not found under /usr/share/icons" >&2
fi

mkdir -p "$out_dir"
appimage_arch="${ARCH:-x86_64}"
ARCH="$appimage_arch" appimagetool "$appdir" "$out_dir/nixalarm-${appimage_arch}.AppImage"
