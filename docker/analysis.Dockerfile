# Headless Ghidra + PyGhidra for analysing the original game binary.
# Native components are built for the container's architecture, so this runs
# natively on Apple Silicon (linux/arm64) as well as on x86-64 CI runners.
FROM eclipse-temurin:21-jdk-noble

ARG GHIDRA_VERSION=12.1.4
ARG GHIDRA_BUILD=20260921
ARG GHIDRA_SHA256=ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential bison flex unzip curl ca-certificates python3 python3-venv \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /tmp/ghidra.zip \
      "https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_${GHIDRA_VERSION}_build/ghidra_${GHIDRA_VERSION}_PUBLIC_${GHIDRA_BUILD}.zip" \
 && echo "${GHIDRA_SHA256}  /tmp/ghidra.zip" | sha256sum -c - \
 && unzip -q /tmp/ghidra.zip -d /opt \
 && mv /opt/ghidra_${GHIDRA_VERSION}_PUBLIC /opt/ghidra \
 && rm /tmp/ghidra.zip

ENV GHIDRA_INSTALL_DIR=/opt/ghidra
RUN if [ "$(uname -m)" != "x86_64" ]; then \
      cd /opt/ghidra/support/gradle && ./gradlew --no-daemon buildNatives \
      && rm -rf /root/.gradle; \
    fi

RUN python3 -m venv /opt/venv \
 && /opt/venv/bin/pip install --no-cache-dir \
      -f /opt/ghidra/Ghidra/Features/PyGhidra/pypkg/dist pyghidra pefile==2024.8.26
ENV PATH=/opt/venv/bin:$PATH

WORKDIR /work
