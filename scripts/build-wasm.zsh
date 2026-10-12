#!/usr/bin/env zsh
# Build the WebAssembly port in the Emscripten container and run the unit tests under node.
# Output: build/wasm/engine/egatrek.{html,js,wasm}. Serve with: scripts/build-wasm.zsh --serve
set -euo pipefail
ROOT=${0:A:h:h}
IMAGE=trek-wasm
TAG=4.0.15
[[ $(uname -m) == arm64 ]] && TAG=4.0.15-arm64
if ! docker image inspect $IMAGE >/dev/null 2>&1; then
  docker build -q --build-arg EMSDK_TAG=$TAG -f $ROOT/docker/wasm.Dockerfile -t $IMAGE $ROOT/docker >/dev/null
fi
docker run --rm -v "$ROOT:/src" -w /src -u "$(id -u):$(id -g)" -e HOME=/tmp $IMAGE sh -c '
  set -e
  cmake --preset wasm
  cmake --build --preset wasm
  node build/wasm/engine/trek_tests.js'
print "Built: $ROOT/build/wasm/engine/egatrek.html"
if [[ ${1:-} == --serve ]]; then
  exec python3 -m http.server -d $ROOT/build/wasm/engine 8080
fi
