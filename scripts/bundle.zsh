#!/usr/bin/env zsh
# Package the app into dist/ as a signed (ad-hoc unless TREK_SIGN_IDENTITY is set)
# application bundle, zip archive, and drag-and-drop disk image (DMG).
# TREK.EXE is never bundled: the app reads it from TREK_DATA or the folder you choose.
set -euo pipefail
ROOT=${0:A:h:h}

$ROOT/scripts/build.zsh

APP="$ROOT/build/macos/engine/EGA Trek.app"
ENTITLEMENTS="$ROOT/engine/resources/egatrek.entitlements"
IDENTITY=${TREK_SIGN_IDENTITY:--}

SIGN_ARGS=(--force --deep --options runtime)
if [[ -f "$ENTITLEMENTS" ]]; then
  SIGN_ARGS+=(--entitlements "$ENTITLEMENTS")
fi

if [[ "$IDENTITY" == "-" ]]; then
  SIGN_ARGS+=(--timestamp=none)
else
  SIGN_ARGS+=(--timestamp)
fi

codesign "${SIGN_ARGS[@]}" --sign "$IDENTITY" "$APP"

mkdir -p "$ROOT/dist"
rm -rf "$ROOT/dist/EGA Trek.app" "$ROOT/dist/EGA Trek.zip" "$ROOT/dist/EGA Trek.dmg"

cp -a "$APP" "$ROOT/dist/EGA Trek.app"
ditto -c -k --keepParent "$APP" "$ROOT/dist/EGA Trek.zip"

# Create drag-and-drop disk image (DMG)
DMG_STAGING=$(mktemp -d "${TMPDIR:-/tmp}/egatrek-dmg.XXXXXX")
trap 'rm -rf "$DMG_STAGING"' EXIT INT TERM

cp -a "$APP" "$DMG_STAGING/EGA Trek.app"
ln -s /Applications "$DMG_STAGING/Applications"

if [[ -f "$APP/Contents/Resources/AppIcon.icns" ]]; then
  cp "$APP/Contents/Resources/AppIcon.icns" "$DMG_STAGING/.VolumeIcon.icns"
  if command -v SetFile &>/dev/null; then
    SetFile -a C "$DMG_STAGING"
  fi
fi

hdiutil create -volname "EGA Trek" \
  -srcfolder "$DMG_STAGING" \
  -ov -format UDZO \
  "$ROOT/dist/EGA Trek.dmg" >/dev/null

if [[ "$IDENTITY" != "-" ]]; then
  codesign --force --sign "$IDENTITY" --timestamp "$ROOT/dist/EGA Trek.dmg"
fi

print "Packaged: $ROOT/dist/EGA Trek.app, $ROOT/dist/EGA Trek.zip, and $ROOT/dist/EGA Trek.dmg (signed with: $IDENTITY)"
