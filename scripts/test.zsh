#!/usr/bin/env zsh
# Run all tests. Default: natively on macOS. --docker: inside the Linux CI container.
set -euo pipefail
ROOT=${0:A:h:h}
cd $ROOT

if [[ ${1:-} == --docker ]]; then
  docker build -q -f docker/ci.Dockerfile -t trek-ci docker >/dev/null
  exec docker run --rm -v "$ROOT:/src:ro" trek-ci sh -c '
    set -e
    mkdir /tmp/w && cd /src && tar --exclude=./build --exclude=./out --exclude=./ghidra-projects \
      --exclude=.venv -cf - . | tar -xf - -C /tmp/w && cd /tmp/w
    uv --directory tools/trekre run pytest -q
    cmake --preset linux -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
    cmake --build --preset linux
    ctest --preset linux'
fi

mise exec -- uv --directory tools/trekre run pytest -q
mise exec -- cmake --preset macos >/dev/null
mise exec -- cmake --build --preset macos
mise exec -- ctest --preset macos
