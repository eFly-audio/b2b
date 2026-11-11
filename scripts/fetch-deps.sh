#!/bin/bash
# Downloads the exact third-party versions b2b is built and tested against into ./deps
# Usage: bash scripts/fetch-deps.sh      (safe to re-run; existing folders are skipped)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p deps

# name  url  tag(s) to try, in order
fetch() {
  local name="$1" url="$2"; shift 2
  if [ -d "deps/$name" ]; then echo "deps/$name already present"; return; fi
  for ref in "$@"; do
    echo "Fetching $name @ $ref"
    if git clone --depth 1 --branch "$ref" --recurse-submodules --shallow-submodules "$url" "deps/$name"; then return; fi
    rm -rf "deps/$name"
  done
  echo "Could not fetch $name ($url)"; exit 1
}

fetch JUCE7          https://github.com/juce-framework/JUCE.git                 7.0.12
fetch libdatachannel https://github.com/paullouisageneau/libdatachannel.git     v0.24.6
fetch libvpx         https://chromium.googlesource.com/webm/libvpx              v1.14.1
fetch mbedtls        https://github.com/Mbed-TLS/mbedtls.git                    mbedtls-3.6.4 v3.6.4
fetch opus           https://github.com/xiph/opus.git                           v1.5.2
echo "All dependencies ready in ./deps"
