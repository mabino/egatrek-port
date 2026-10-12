# Emscripten toolchain with a CMake new enough for our presets.
# Build with --build-arg EMSDK_TAG=4.0.15-arm64 on Apple Silicon (scripts/build-wasm.zsh does this).
ARG EMSDK_TAG=4.0.15
FROM emscripten/emsdk:${EMSDK_TAG}
RUN pip3 install --no-cache-dir cmake==3.31.6 ninja==1.11.1.4
