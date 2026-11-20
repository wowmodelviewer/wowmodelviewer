#!/bin/bash
# Makes the self-contained macOS app and its disk image from a build:
#   installer/macos/make-bundle.sh <build dir> <output dir>
# The bundle carries every library it needs (Qt, wxWidgets, the Vulkan loader and MoltenVK, libpng...), so it runs on a
# Mac without Homebrew. It is signed ad hoc only -- no Developer ID, no notarization -- so the first launch is
# right-click > Open (or System Settings > Privacy & Security > Open Anyway).
set -euo pipefail

BUILD="${1:?build dir}"
OUT="${2:?output dir}"
APP_NAME="WoW Model Viewer"
SRC_APP="$BUILD/Source/wowmodelviewer/$APP_NAME.app"
[ -d "$SRC_APP" ] || { echo "no app at $SRC_APP" >&2; exit 1; }

BREW="$(brew --prefix)"
QT="$(brew --prefix qt@5)"
MOLTENVK="$(brew --prefix molten-vk)"

mkdir -p "$OUT"
APP="$OUT/$APP_NAME.app"
rm -rf "${APP:?}"
cp -R "$SRC_APP" "$APP"

# What a run may have left inside the build's bundle (the home folder is per user, never the bundle).
rm -rf "${APP:?}/Contents/MacOS/"{userSettings,dbcache,games,dbd} "${APP:?}/Contents/MacOS/"*.csv*

# Qt's deployment tool: Qt's frameworks and plugins, and every other library the program and its plugins link
# (wxWidgets, the Vulkan loader, libpng, libjpeg, the viewer's own core and wow libraries), copied into
# Contents/Frameworks with their load paths rewritten.
EXTRA=()
for p in "$APP/Contents/PlugIns/"*.dylib; do EXTRA+=("-executable=$p"); done
"$QT/bin/macdeployqt" "$APP" "${EXTRA[@]}" -always-overwrite \
  -libpath="$BUILD/Source/core" -libpath="$BUILD/Source/games/wow" -libpath="$BREW/lib"

# MoltenVK, the Vulkan driver, found by the loader through the bundle's own ICD manifest.
cp "$MOLTENVK/lib/libMoltenVK.dylib" "$APP/Contents/Frameworks/"
mkdir -p "$APP/Contents/Resources/vulkan/icd.d"
cat > "$APP/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json" <<'JSON'
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "../../../Frameworks/libMoltenVK.dylib",
    "api_version": "1.2.0",
    "is_portability_driver": true
  }
}
JSON

# Nothing may still point into Homebrew or the build tree. (otool -D names a library's own id, which otool -L also lists
# first; that line is not a dependency.)
LEFT=""
while IFS= read -r -d '' f; do
  self=$(otool -D "$f" 2>/dev/null | sed -n 2p)
  deps=$(otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' | grep -vxF "${self:-//none//}" || true)
  bad=$(echo "$deps" | grep -E "^($BREW|$PWD|/usr/local)" || true)
  [ -n "$bad" ] && LEFT+="$f -> $bad"$'\n'
done < <(find "$APP" -type f \( -name '*.dylib' -o -perm -u+x \) -print0)
if [ -n "$LEFT" ]; then
  echo "libraries still linked from outside the bundle:" >&2
  printf '%s' "$LEFT" >&2
  exit 1
fi

# Ad hoc signature (no identity): what Apple silicon requires for any code to run at all.
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

VERSION=$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$APP/Contents/Info.plist")
DMG="$OUT/WoWModelViewer-$VERSION-macOS-$(uname -m).dmg"
rm -f "$DMG"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "WoW Model Viewer $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
rm -rf "${STAGE:?}"
shasum -a 256 "$DMG"
