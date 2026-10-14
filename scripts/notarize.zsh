#!/usr/bin/env zsh
# Notarize the bundled app. Needs a Developer ID identity and a notarytool keychain profile:
#   xcrun notarytool store-credentials trek-notary --apple-id you@example.com --team-id TEAMID
#   TREK_SIGN_IDENTITY="Developer ID Application: ..." scripts/notarize.zsh
set -euo pipefail
ROOT=${0:A:h:h}
PROFILE=${TREK_NOTARY_PROFILE:-trek-notary}
[[ -n ${TREK_SIGN_IDENTITY:-} ]] || { print -u2 "set TREK_SIGN_IDENTITY to a Developer ID Application identity"; exit 1; }

$ROOT/scripts/bundle.zsh

# Submit the DMG to Apple Notary Service
xcrun notarytool submit "$ROOT/dist/EGA Trek.dmg" --keychain-profile "$PROFILE" --wait

# Staple notarization tickets to the app bundle and disk image
xcrun stapler staple "$ROOT/build/macos/engine/EGA Trek.app"
xcrun stapler staple "$ROOT/dist/EGA Trek.app"
xcrun stapler staple "$ROOT/dist/EGA Trek.dmg"

# Update zip with the stapled app
ditto -c -k --keepParent "$ROOT/dist/EGA Trek.app" "$ROOT/dist/EGA Trek.zip"

print "Notarized and stapled DMG, app bundle, and zip archive."
