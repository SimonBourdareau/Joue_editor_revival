#!/bin/bash
# Makes a disk image (macOS only, it needs hdiutil) from the macOS package made by package-release.sh.
#
#   packaging/make-dmg.sh dist/joue-revival-macos-arm64.zip dist
set -eu
zip=${1:?the macOS package}
out=${2:?output directory}
mkdir -p "$out"
out=$(cd "$out" && pwd)
name=$(basename "$zip" .zip)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir "$stage/Joue Revival"
unzip -q "$zip" -d "$stage/Joue Revival"
ln -s /Applications "$stage/Joue Revival/Applications"
hdiutil create -volname "Joue Revival" -srcfolder "$stage/Joue Revival" -ov -format UDZO "$out/$name.dmg" >&2
echo "$out/$name.dmg"
