#!/bin/bash
# Fetch build dependencies that are not vendored:
#   - MacKernelSDK (git submodule)
#   - Lilu SDK (headers + plugin_start.cpp from the Lilu DEBUG release),
#     needed by SimpleMtkBtPatcher.
set -euo pipefail
cd "$(dirname "$0")/.."

git submodule update --init --recursive

LILU_VERSION="${LILU_VERSION:-1.7.2}"
if [ ! -d Lilu.kext/Contents/Resources/Headers ]; then
    tmp="$(mktemp -d)"
    curl -fsSL -o "$tmp/lilu.zip" \
        "https://github.com/acidanthera/Lilu/releases/download/${LILU_VERSION}/Lilu-${LILU_VERSION}-DEBUG.zip"
    ditto -x -k "$tmp/lilu.zip" "$tmp"
    rm -rf Lilu.kext
    mv "$tmp/Lilu.kext" .
    rm -rf "$tmp"
fi
echo "dependencies ready (Lilu ${LILU_VERSION})"
