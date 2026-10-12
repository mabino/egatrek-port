# Linux build + test environment (used by CI and `scripts/test.zsh --docker`).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential clang cmake ninja-build git ca-certificates curl python3 \
 && rm -rf /var/lib/apt/lists/*

COPY --from=ghcr.io/astral-sh/uv:0.9 /uv /usr/local/bin/uv
ENV UV_PYTHON_DOWNLOADS=automatic UV_LINK_MODE=copy

WORKDIR /src
