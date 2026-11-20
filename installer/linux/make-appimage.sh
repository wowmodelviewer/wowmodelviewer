#!/bin/bash
# Makes the Linux AppImage from a build:  installer/linux/make-appimage.sh <build dir> <wxWidgets prefix> <output dir>
# The AppImage carries Qt, wxWidgets and the viewer's own libraries; the Vulkan loader and drivers are the system's
# (the GPU driver must match the kernel's), as are GTK's theme and X11/XWayland.
set -euo pipefail

BUILD="$(cd "${1:?build dir}" && pwd)"
WX="${2:?wxWidgets prefix}"
OUT="${3:?output dir}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APPDIR="$(mktemp -d)/AppDir"
mkdir -p "$APPDIR/usr/bin/plugins" "$APPDIR/usr/lib" "$APPDIR/usr/share/wowmodelviewer" \
         "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps" "$OUT"

EXE_DIR="$BUILD/Source/wowmodelviewer"
cp "$EXE_DIR/wowmodelviewer" "$APPDIR/usr/bin/"
cp "$EXE_DIR/plugins/"*.so "$APPDIR/usr/bin/plugins/"
cp "$BUILD/Source/core/libcore.so" "$BUILD/Source/games/wow/libwow.so" "$APPDIR/usr/lib/"
cp -R "$EXE_DIR/share/." "$APPDIR/usr/share/wowmodelviewer/"
cp "$ROOT/bin_support/Icons/WMW-Midnight-26-256x256.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/wowmodelviewer.png"
cat > "$APPDIR/usr/share/applications/wowmodelviewer.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=WoW Model Viewer
Comment=View World of Warcraft models from your own game installation
Exec=wowmodelviewer
Icon=wowmodelviewer
Categories=Graphics;3DGraphics;Viewer;
Terminal=false
DESKTOP

curl -fsSL -o /tmp/linuxdeploy https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
curl -fsSL -o /tmp/linuxdeploy-plugin-qt https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x /tmp/linuxdeploy /tmp/linuxdeploy-plugin-qt

export LD_LIBRARY_PATH="$WX/lib:$APPDIR/usr/lib:${LD_LIBRARY_PATH:-}"
export QMAKE="$(command -v qmake || command -v qmake-qt5)"
export APPIMAGE_EXTRACT_AND_RUN=1
cd "$OUT"
LIBS=()
for p in "$APPDIR/usr/bin/plugins/"*.so; do LIBS+=(--library "$p"); done
/tmp/linuxdeploy --appdir "$APPDIR" --plugin qt "${LIBS[@]}" \
  --exclude-library 'libvulkan.so*' \
  --desktop-file "$APPDIR/usr/share/applications/wowmodelviewer.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/wowmodelviewer.png" \
  --output appimage
sha256sum ./*.AppImage
