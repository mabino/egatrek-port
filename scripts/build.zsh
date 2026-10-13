#!/usr/bin/env zsh
# Recompile TREK.EXE (if found) and build the native macOS (Apple Silicon) app.
set -euo pipefail
setopt extendedglob
ROOT=${0:A:h:h}
cd $ROOT
export TREK_DATA=${TREK_DATA:-${ROOT:h}}
if [[ -n ${TREK_DATA}(#qN) ]] && ls $TREK_DATA | grep -qi '^trek.exe$'; then
  $ROOT/scripts/recomp.zsh
else
  print -u2 "TREK.EXE not found in $TREK_DATA: building the app without the game"
fi
mise exec -- cmake --preset macos
mise exec -- cmake --build --preset macos
print "Built: $ROOT/build/macos/engine/EGA Trek.app"
