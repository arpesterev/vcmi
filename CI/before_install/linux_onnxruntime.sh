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
ONNXRUNTIME_ARCHIVE=$(mktemp "${RUNNER_TEMP:-/tmp}/onnxruntime.XXXXXX.tgz")
curl -fsSL --connect-timeout 30 --max-time 300 --retry 3 --retry-max-time 900 "$ONNXRUNTIME_URL" -o "$ONNXRUNTIME_ARCHIVE"
sudo tar -xzv --strip-components=1 -C "$ONNXRUNTIME_ROOT" -f "$ONNXRUNTIME_ARCHIVE"
rm -f "$ONNXRUNTIME_ARCHIVE"

