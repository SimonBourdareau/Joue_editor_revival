#!/bin/bash
# Makes the release package of a platform from a finished build: the program, the README of the platform, the two guides, the pictures and
# the licence. Used by the GitHub workflow and by hand.
#
#   packaging/package-release.sh linux|windows|macos BUILD_DIR OUT_DIR
#
# Writes OUT_DIR/joue-revival-<platform>-<arch>.tar.gz (Linux) or .zip (Windows, macOS) and prints its path.
# macOS: the bundle is signed ad hoc with codesign, or with rcodesign when that is what is installed (Linux cross-builds);
#        MACOS_ARCH (arm64 or x86_64) names the package when the build was not made on the machine that packages it.
set -eu
cd "$(dirname "$0")/.."
platform=${1:?platform: linux, windows or macos}
build=${2:?build directory}
out=${3:?output directory}
# the version of the project: the one on the project( line, not the minimum version of CMake
version=$(sed -n 's/^project([^ ]* VERSION \([0-9.]*\).*/\1/p' cpp/CMakeLists.txt)
documents="docs/user-guide.md docs/module-designer.md docs/user-guide.fr.md docs/module-designer.fr.md docs/editor-preview.png docs/designer-preview.png docs/editor-preview-fr.png docs/designer-preview-fr.png LICENSE"
put_readmes() {
    sed "s/README-$platform.fr.md/README.fr.md/g" "docs/README-$platform.md" > "$1/README.md"
    sed "s/README-$platform.md/README.md/g" "docs/README-$platform.fr.md" > "$1/README.fr.md"
}
mkdir -p "$out"
out=$(cd "$out" && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

case "$platform" in
  linux)
    arch=$(uname -m)
    name="joue-revival-linux-$arch"
    mkdir -p "$stage/$name"
    cp "$build/joue_editor" "$stage/$name/joue_editor"
    chmod +x "$stage/$name/joue_editor"
    mkdir -p "$stage/$name/presets" && cp presets/factoryLibrary.xml "$stage/$name/presets/"
    put_readmes "$stage/$name"
    cp $documents "$stage/$name/"
    (cd "$stage" && tar -czf "$out/$name.tar.gz" "$name")
    echo "$out/$name.tar.gz"
    ;;
  windows)
    name="joue-revival-windows-x64"
    exe="$build/joue_editor.exe"
    [ -f "$exe" ] || exe="$build/Release/joue_editor.exe"
    [ -f "$exe" ] || { echo "joue_editor.exe not found in $build"; exit 1; }
    mkdir -p "$stage/$name"
    cp "$exe" "$stage/$name/JoueRevival.exe"
    mkdir -p "$stage/$name/presets" && cp presets/factoryLibrary.xml "$stage/$name/presets/"
    cp packaging/windows/JoueRevival-safe-graphics.bat "$stage/$name/"
    put_readmes "$stage/$name"
    cp $documents "$stage/$name/"
    rm -f "$out/$name.zip"
    (cd "$stage" && zip -qr "$out/$name.zip" "$name")
    echo "$out/$name.zip"
    ;;
  macos)
    arch=${MACOS_ARCH:-$(uname -m)}
    name="joue-revival-macos-$arch"
    app="$stage/Joue Revival.app"
    mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
    cp "$build/joue_editor" "$app/Contents/MacOS/joue_editor"
    chmod +x "$app/Contents/MacOS/joue_editor"
    cp cpp/app/icons/joue-revival.icns "$app/Contents/Resources/AppIcon.icns"
    mkdir -p "$app/Contents/Resources/presets" && cp presets/factoryLibrary.xml "$app/Contents/Resources/presets/"
    sed "s/@VERSION@/$version/g" packaging/macos/Info.plist.in > "$app/Contents/Info.plist"
    # a bundle needs a signature that seals Info.plist and the resources; without one macOS reports the app as damaged
    if command -v codesign >/dev/null 2>&1; then
      codesign --force --deep --sign - "$app"
    elif command -v rcodesign >/dev/null 2>&1; then
      rcodesign sign "$app" "$app.signed" && rm -rf "$app" && mv "$app.signed" "$app"
    else
      echo "neither codesign nor rcodesign found: the app is not signed" >&2
    fi
    put_readmes "$stage"
    cp $documents "$stage/"
    rm -f "$out/$name.zip"
    (cd "$stage" && zip -qry "$out/$name.zip" "Joue Revival.app" README.md README.fr.md user-guide.md module-designer.md user-guide.fr.md module-designer.fr.md editor-preview.png designer-preview.png editor-preview-fr.png designer-preview-fr.png LICENSE)
    echo "$out/$name.zip"
    ;;
  *) echo "platform: linux, windows or macos"; exit 1 ;;
esac
