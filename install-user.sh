#!/usr/bin/env bash
# User-local install (no root): puts vantaviewer on PATH and registers its .desktop +
# icon so launchers and file managers (KDE/Dolphin "Open With", default app) find it.
# Build first:  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
bin="$here/build/vantaviewer"
if [ ! -x "$bin" ]; then
  echo "Build first:  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build" >&2
  exit 1
fi

apps="$HOME/.local/share/applications"
icons="$HOME/.local/share/icons/hicolor"
mkdir -p "$HOME/.local/bin" "$apps"

# Symlink the binary (so rebuilds are picked up automatically).
ln -sf "$bin" "$HOME/.local/bin/vantaviewer"

# .desktop with an absolute Exec so it launches regardless of PATH.
sed "s|^Exec=vantaviewer|Exec=$HOME/.local/bin/vantaviewer|" \
    "$here/vantaviewer.desktop" > "$apps/vantaviewer.desktop"

# Icon into the hicolor theme.
for s in 64 128 256 512; do
  d="$icons/${s}x${s}/apps"; mkdir -p "$d"
  cp -f "$here/icons/vantaviewer-${s}.png" "$d/vantaviewer.png"
done

# Refresh caches (best-effort).
command -v update-desktop-database >/dev/null && update-desktop-database "$apps" || true
command -v gtk-update-icon-cache    >/dev/null && gtk-update-icon-cache -f -t "$icons" 2>/dev/null || true
command -v kbuildsycoca6            >/dev/null && kbuildsycoca6 >/dev/null 2>&1 || true

echo "Installed. Search 'vantaviewer' in your launcher, or set it as default image"
echo "viewer in Dolphin (right-click an image -> Open With -> set as default), or:"
echo "  xdg-mime default vantaviewer.desktop image/png image/jpeg image/avif image/jxl \\"
echo "    image/heic image/heif image/webp image/tiff image/bmp"
