#!/usr/bin/env zsh
# Grow the recompiler's seed list by playing the game headless: entry points
# the interpreter had to run become seeds, until a run finds no new ones.
# Each round is checked against the interpreter with trek_lockstep.
# Usage: scripts/discover-seeds.zsh [--keys SCRIPT] [--frames N]; result in out/seeds.txt
set -euo pipefail
ROOT=${0:A:h:h}
cd $ROOT
export TREK_DATA=${TREK_DATA:-${ROOT:h}}
KEYS="" FRAMES=600
while (( $# )); do
  case $1 in
    --keys) KEYS=$2; shift 2 ;;
    --frames) FRAMES=$2; shift 2 ;;
    *) print -u2 "unknown option: $1"; exit 2 ;;
  esac
done
mkdir -p out && touch out/seeds.txt
for round in {1..6}; do
  mise exec -- uv --directory tools/trekre run trekre recomp --out $ROOT/build/generated --extra-seeds $ROOT/out/seeds.txt
  mise exec -- cmake --build --preset macos --target trek_headless --target trek_lockstep >/dev/null
  mise exec -- build/macos/engine/trek_lockstep --keys "$KEYS" | tail -1
  before=$(sort -u out/seeds.txt | wc -l)
  mise exec -- build/macos/engine/trek_headless --frames $FRAMES --keys "$KEYS" --dump-interpreted out/seeds.txt >/dev/null 2>&1 || true
  sort -u out/seeds.txt -o out/seeds.txt
  after=$(wc -l < out/seeds.txt)
  print "round $round: $after seeds"
  (( after == before )) && break
done
