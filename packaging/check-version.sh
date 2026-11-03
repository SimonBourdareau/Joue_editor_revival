#!/bin/bash
# Checks that a release tag (v0.36.0) is the version of the project in cpp/CMakeLists.txt.
# A name that is not a version tag (a branch, on a manual run) passes: there is nothing to check.
#
#   packaging/check-version.sh v0.36.0
set -eu
cd "$(dirname "$0")/.."
ref=${1:-}
version=$(sed -n 's/^project([^ ]* VERSION \([0-9.]*\).*/\1/p' cpp/CMakeLists.txt)
[ -n "$version" ] || { echo "No version found in cpp/CMakeLists.txt."; exit 1; }
case "$ref" in
  v[0-9]*)
    if [ "$ref" != "v$version" ]; then
      echo "The tag $ref is not the version of the project (v$version). Raise or fix the version in cpp/CMakeLists.txt, commit, and tag again."
      exit 1
    fi
    echo "$ref is the version of the project."
    ;;
  *) echo "'$ref' is not a version tag: nothing to check (the project is version $version)." ;;
esac
