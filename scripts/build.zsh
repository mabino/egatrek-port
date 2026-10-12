#!/usr/bin/env zsh
# Configure and build the native macOS (Apple Silicon) port.
set -euo pipefail
ROOT=${0:A:h:h}
cd $ROOT
mise exec -- cmake --preset macos
mise exec -- cmake --build --preset macos
print "Built: $ROOT/build/macos/engine/EGA Trek.app"
