#!/usr/bin/env zsh
# Package the app into dist/ as a signed (ad-hoc unless TREK_SIGN_IDENTITY is set) zip.
# TREK.EXE is never bundled: the app reads it from TREK_DATA or the folder you choose.
set -euo pipefail
ROOT=${0:A:h:h}
$ROOT/scripts/build.zsh
APP="$ROOT/build/macos/engine/EGA Trek.app"
IDENTITY=${TREK_SIGN_IDENTITY:--}
codesign --force --deep --options runtime --timestamp=none --sign "$IDENTITY" "$APP"
mkdir -p $ROOT/dist
ditto -c -k --keepParent "$APP" "$ROOT/dist/EGA Trek.zip"
print "Packaged: $ROOT/dist/EGA Trek.zip (signed with: $IDENTITY)"
