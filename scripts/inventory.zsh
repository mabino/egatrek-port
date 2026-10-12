#!/usr/bin/env zsh
# Segment map, embedded BGI assets, Pascal strings and the Triplex font, into out/.
set -euo pipefail
ROOT=${0:A:h:h}
export TREK_DATA=${TREK_DATA:-${ROOT:h}}
for cmd in inventory strings font; do
  out=$ROOT/out/$cmd.json
  [[ $cmd == font ]] && out=$ROOT/out/trip-font.json
  mise exec -- uv --directory $ROOT/tools/trekre run trekre $cmd --out $out
done
