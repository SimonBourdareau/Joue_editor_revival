#!/bin/bash
# Writes the notes of a release on the standard output: what the packages are, and the commits since the previous tag.
#
#   packaging/release-notes.sh v0.36.0 > notes.md
set -eu
cd "$(dirname "$0")/.."
tag=${1:?the tag of the release}
previous=$(git describe --tags --abbrev=0 "$tag^" 2>/dev/null || true)
echo "# Joué Revival ${tag#v}"
echo
cat <<'TEXT'
Free software under the GNU GPL v3 or later. Each package has the README and the two guides, in English and in French, and the licence.

| Package | For |
|---|---|
| `joue-revival-windows-x64-setup.exe` | Windows 64-bit, with an installer (when it could be made) |
| `joue-revival-windows-x64.zip` | Windows 64-bit, no installer: unzip and run `JoueRevival.exe` |
| `joue-revival-macos-arm64.dmg`, `.zip` | macOS, Apple silicon. Without a notarization the first start needs `xattr -cr "Joue Revival.app"` |
| `joue-revival-macos-x86_64.dmg`, `.zip` | macOS, Intel (when the runner was available) |
| `joue-revival-linux-x86_64.AppImage`, `.tar.gz` | Linux, 64-bit, on X11 or XWayland |
| `joue-revival-*-source.zip` | the source of this version |

The factory libraries of the manufacturer are not part of any package: open the one that comes with the original Joué Editor.
`SHA256SUMS.txt` has the checksum of every file.

## Changes
TEXT
if [ -z "$previous" ]; then
  echo "First release."
else
  git log --no-merges --format='- %s' "$previous..$tag" | sed 's/^- v[0-9][0-9.]*: /- /'
fi
