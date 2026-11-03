#!/bin/bash
# Signs the app of a macOS package with a Developer ID certificate, has Apple notarize it, staples the ticket, and puts the app back in the package.
# Used by the release workflow only when the secrets are set; it needs macOS and a paid Apple developer account.
#
#   MACOS_CERTIFICATE            the Developer ID Application certificate (.p12), in base64
#   MACOS_CERTIFICATE_PASSWORD   its password
#   MACOS_SIGN_IDENTITY          for instance: Developer ID Application: Your Name (TEAMID)
#   APPLE_ID, APPLE_TEAM_ID      the account
#   APPLE_APP_PASSWORD           an app-specific password of the account
#
#   packaging/notarize-macos.sh dist/joue-revival-macos-arm64.zip
set -eu
zip=${1:?the macOS package}
zip=$(cd "$(dirname "$zip")" && pwd)/$(basename "$zip")
: "${MACOS_CERTIFICATE:?}" "${MACOS_CERTIFICATE_PASSWORD:?}" "${MACOS_SIGN_IDENTITY:?}" "${APPLE_ID:?}" "${APPLE_TEAM_ID:?}" "${APPLE_APP_PASSWORD:?}"
work=$(mktemp -d)
keychain="$work/signing.keychain-db"
cleanup() { security delete-keychain "$keychain" >/dev/null 2>&1 || true; rm -rf "$work"; }
trap cleanup EXIT
echo "$MACOS_CERTIFICATE" | base64 --decode > "$work/certificate.p12"
security create-keychain -p "ci" "$keychain"
security set-keychain-settings -lut 3600 "$keychain"
security unlock-keychain -p "ci" "$keychain"
security import "$work/certificate.p12" -k "$keychain" -P "$MACOS_CERTIFICATE_PASSWORD" -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "ci" "$keychain" >/dev/null
security list-keychains -d user -s "$keychain" $(security list-keychains -d user | tr -d '"')
mkdir "$work/package"
unzip -q "$zip" -d "$work/package"
app="$work/package/Joue Revival.app"
codesign --force --deep --options runtime --timestamp --sign "$MACOS_SIGN_IDENTITY" "$app"
ditto -c -k --keepParent "$app" "$work/app.zip"
xcrun notarytool submit "$work/app.zip" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" --wait
xcrun stapler staple "$app"
rm -f "$zip"
(cd "$work/package" && zip -qry "$zip" .)
echo "notarized: $zip" >&2
