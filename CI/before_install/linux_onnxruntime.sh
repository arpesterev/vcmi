#!/usr/bin/env bash

# https://github.com/microsoft/onnxruntime/discussions/6489

ARCH=$(uname -m)
if [ "$ARCH" = "x86_64" ]; then
    ONNXRUNTIME_URL="https://github.com/microsoft/onnxruntime/releases/download/v1.18.1/onnxruntime-linux-x64-1.18.1.tgz"
elif [ "$ARCH" = "aarch64" ]; then
    ONNXRUNTIME_URL="https://github.com/microsoft/onnxruntime/releases/download/v1.18.1/onnxruntime-linux-aarch64-1.18.1.tgz"
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

ONNXRUNTIME_ROOT=/opt/onnxruntime
sudo mkdir -p "$ONNXRUNTIME_ROOT"
if [[ -n "${ONNXRUNTIME_ARCHIVE_CACHE:-}" ]]; then
    mkdir -p "$ONNXRUNTIME_ARCHIVE_CACHE"
    ONNXRUNTIME_ARCHIVE="$ONNXRUNTIME_ARCHIVE_CACHE/onnxruntime-1.18.1-$ARCH.tgz"
    if [[ ! -f "$ONNXRUNTIME_ARCHIVE" ]]; then
        curl -fsSL --connect-timeout 30 --max-time 300 --retry 3 --retry-max-time 900 "$ONNXRUNTIME_URL" -o "$ONNXRUNTIME_ARCHIVE"
        sha256sum "$ONNXRUNTIME_ARCHIVE" > "$ONNXRUNTIME_ARCHIVE.sha256"
    fi
    # Integrity of the immutable, branch-owned dependency snapshot. Initial
    # provenance is the pinned official HTTPS release URL above.
    sha256sum --check "$ONNXRUNTIME_ARCHIVE.sha256"
else
    ONNXRUNTIME_ARCHIVE=$(mktemp "${RUNNER_TEMP:-/tmp}/onnxruntime.XXXXXX.tgz")
    curl -fsSL --connect-timeout 30 --max-time 300 --retry 3 --retry-max-time 900 "$ONNXRUNTIME_URL" -o "$ONNXRUNTIME_ARCHIVE"
fi
sudo tar -xzv --strip-components=1 -C "$ONNXRUNTIME_ROOT" -f "$ONNXRUNTIME_ARCHIVE"
if [[ -z "${ONNXRUNTIME_ARCHIVE_CACHE:-}" ]]; then rm -f "$ONNXRUNTIME_ARCHIVE"; fi


