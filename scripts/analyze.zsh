#!/usr/bin/env zsh
# Run headless Ghidra analysis of TREK.EXE in the trek-analysis container.
# Usage: scripts/analyze.zsh [--decompile] [--rebuild] [--project NAME]
# A new --project name starts from a fresh Ghidra import; the default reuses 'trek'.
set -euo pipefail
setopt extendedglob

ROOT=${0:A:h:h}
DATA=${TREK_DATA:-${ROOT:h}}
DATA=${DATA:A}
IMAGE=trek-analysis
EXTRA=()

while (( $# )); do
  arg=$1; shift
  case $arg in
    --project) EXTRA+=(--project-name "$1"); shift ;;
    --rebuild) docker rmi -f $IMAGE >/dev/null 2>&1 || true ;;
    --decompile) EXTRA+=(--decompile) ;;
    *) print -u2 "unknown option: $arg"; exit 2 ;;
  esac
done

EXE=( $DATA/(#i)trek.exe(N) )
(( ${#EXE} )) || { print -u2 "TREK.EXE not found in $DATA (set TREK_DATA)"; exit 1; }

if ! docker image inspect $IMAGE >/dev/null 2>&1; then
  docker build -f $ROOT/docker/analysis.Dockerfile -t $IMAGE $ROOT/docker
fi

mkdir -p $ROOT/out/ghidra $ROOT/ghidra-projects
docker run --rm \
  -v "$DATA:/data:ro" \
  -v "$ROOT/tools:/work/tools:ro" \
  -v "$ROOT/out:/work/out" \
  -v "$ROOT/ghidra-projects:/work/ghidra-projects" \
  $IMAGE python /work/tools/ghidra/analyze.py \
    --binary "/data/${EXE[1]:t}" --out /work/out/ghidra "${EXTRA[@]}"
