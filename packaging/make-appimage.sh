#!/bin/bash
# Makes an AppImage of the Linux build: the program, its guides, a desktop entry and its icon.
# appimagetool must be in the PATH, or named by APPIMAGETOOL.
#
#   packaging/make-appimage.sh BUILD_DIR OUT_DIR
set -eu
cd "$(dirname "$0")/.."
build=${1:?build directory}
out=${2:?output directory}
mkdir -p "$out"
out=$(cd "$out" && pwd)
arch=$(uname -m)
tool=${APPIMAGETOOL:-appimagetool}
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
app="$stage/JoueRevival.AppDir"
mkdir -p "$app/usr/bin" "$app/usr/share/doc/joue-revival" "$app/usr/share/joue-revival/presets"
cp presets/factoryLibrary.xml "$app/usr/share/joue-revival/presets/"
cp "$build/joue_editor" "$app/usr/bin/joue_editor"
chmod +x "$app/usr/bin/joue_editor"
cp packaging/linux/joue-revival.desktop "$app/joue-revival.desktop"
cp cpp/app/icons/joue-revival-1024.png "$app/joue-revival.png"
cp docs/README-linux.md "$app/usr/share/doc/joue-revival/README.md"
cp docs/README-linux.fr.md "$app/usr/share/doc/joue-revival/README.fr.md"
cp docs/user-guide.md docs/user-guide.fr.md docs/module-designer.md docs/module-designer.fr.md docs/editor-preview.png docs/designer-preview.png docs/editor-preview-fr.png docs/designer-preview-fr.png LICENSE "$app/usr/share/doc/joue-revival/"
printf '#!/bin/sh\nexec "$(dirname "$(readlink -f "$0")")/usr/bin/joue_editor" "$@"\n' > "$app/AppRun"
chmod +x "$app/AppRun"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$arch" "$tool" "$app" "$out/joue-revival-linux-$arch.AppImage" >&2
echo "$out/joue-revival-linux-$arch.AppImage"
