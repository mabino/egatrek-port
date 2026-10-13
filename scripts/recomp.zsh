#!/usr/bin/env zsh
# Recompile the user's TREK.EXE into C++ (build/generated). The output is
# derived from the game, so it stays out of git like the executable itself.
set -euo pipefail
ROOT=${0:A:h:h}
export TREK_DATA=${TREK_DATA:-${ROOT:h}}
EXTRA=()
[[ -s $ROOT/out/seeds.txt ]] && EXTRA=(--extra-seeds $ROOT/out/seeds.txt)
mise exec -- uv --directory $ROOT/tools/trekre run trekre recomp --out $ROOT/build/generated "${EXTRA[@]}"
