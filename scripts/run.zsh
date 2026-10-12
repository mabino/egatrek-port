#!/usr/bin/env zsh
# Build (if needed) and run the port against the original TREK.EXE.
set -euo pipefail
ROOT=${0:A:h:h}
export TREK_DATA=${TREK_DATA:-${ROOT:h}}
APP="$ROOT/build/macos/engine/EGA Trek.app"
[[ -d $APP ]] || $ROOT/scripts/build.zsh
exec "$APP/Contents/MacOS/EGA Trek" "$@"
