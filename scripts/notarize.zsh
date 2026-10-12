#!/usr/bin/env zsh
# Notarize the bundled app. Needs a Developer ID identity and a notarytool keychain profile:
#   xcrun notarytool store-credentials trek-notary --apple-id you@example.com --team-id TEAMID
#   TREK_SIGN_IDENTITY="Developer ID Application: ..." scripts/notarize.zsh
set -euo pipefail
ROOT=${0:A:h:h}
PROFILE=${TREK_NOTARY_PROFILE:-trek-notary}
[[ -n ${TREK_SIGN_IDENTITY:-} ]] || { print -u2 "set TREK_SIGN_IDENTITY to a Developer ID Application identity"; exit 1; }
$ROOT/scripts/bundle.zsh
xcrun notarytool submit "$ROOT/dist/EGA Trek.zip" --keychain-profile "$PROFILE" --wait
xcrun stapler staple "$ROOT/build/macos/engine/EGA Trek.app"
ditto -c -k --keepParent "$ROOT/build/macos/engine/EGA Trek.app" "$ROOT/dist/EGA Trek.zip"
print "Notarized and stapled."
