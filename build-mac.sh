#!/bin/bash
# b2b by eFly Audio - one-command macOS build (Apple Silicon or Intel)
# Usage:  cd into this folder in Terminal, then:  ./build-mac.sh
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"
LOG="$ROOT/build-mac.log"
: > "$LOG"
step() { echo; echo "==> $1"; echo "==> $1" >> "$LOG"; }
fail() { echo; echo "BUILD FAILED during: $1"; echo "Open an issue on GitHub and paste the last ~40 lines of build-mac.log:"; tail -n 40 "$LOG"; exit 1; }

echo "b2b - macOS build"
ARCH="$(uname -m)"
JOBS="$(sysctl -n hw.ncpu)"
echo "Mac: $(sw_vers -productVersion), $ARCH, $JOBS cores"

# 1. Apple's command line tools (compiler + macOS SDK)
if ! xcode-select -p >/dev/null 2>&1; then
  echo "Installing Apple Command Line Tools - click 'Install' in the window that pops up."
  xcode-select --install || true
  echo "When that install finishes, run ./build-mac.sh again."
  exit 0
fi

# 2. cmake + ninja into a private folder (nothing installed system-wide)
step "Build tools (cmake, ninja)"
if [ ! -x "$ROOT/.tools/bin/cmake" ]; then
  python3 -m venv "$ROOT/.tools" >> "$LOG" 2>&1 || fail "python venv"
  "$ROOT/.tools/bin/pip" install -q --upgrade pip >> "$LOG" 2>&1 || true
  "$ROOT/.tools/bin/pip" install -q cmake ninja >> "$LOG" 2>&1 || fail "installing cmake/ninja"
fi
export PATH="$ROOT/.tools/bin:$PATH"

# 3a. third-party sources (downloaded once into ./deps)
if [ ! -d "$ROOT/deps/JUCE7" ] || [ ! -d "$ROOT/deps/libdatachannel" ] || [ ! -d "$ROOT/deps/libvpx" ] \
   || [ ! -d "$ROOT/deps/mbedtls" ] || [ ! -d "$ROOT/deps/opus" ]; then
  step "Downloading dependencies (first run only)"
  bash "$ROOT/scripts/fetch-deps.sh" >> "$LOG" 2>&1 || fail "downloading dependencies"
fi

PREFIX="$ROOT/deps-install"
mkdir -p "$PREFIX" "$ROOT/build"
export MACOSX_DEPLOYMENT_TARGET=11.0
COMMON=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX"
        -DCMAKE_OSX_ARCHITECTURES="$ARCH" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DCMAKE_POLICY_VERSION_MINIMUM=3.5)

lib() { # name srcdir extra-args...
  local name="$1" src="$2"; shift 2
  step "$name"
  cmake -S "$src" -B "build/$name" "${COMMON[@]}" "$@" >> "$LOG" 2>&1 || fail "$name (configure)"
  cmake --build "build/$name" >> "$LOG" 2>&1 || fail "$name (compile)"
  cmake --install "build/$name" >> "$LOG" 2>&1 || fail "$name (install)"
}

# 3. dependencies (all static, all from the bundled sources)
lib mbedtls deps/mbedtls -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DMBEDTLS_FATAL_WARNINGS=OFF
lib opus deps/opus -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF
lib libdatachannel deps/libdatachannel -DUSE_MBEDTLS=ON -DNO_MEDIA=ON -DNO_EXAMPLES=ON -DNO_TESTS=ON -DBUILD_SHARED_LIBS=OFF

step "libvpx (video codec)"
if [ ! -f "$PREFIX/lib/libvpx.a" ]; then
  if [ "$ARCH" = "arm64" ]; then VPXT=arm64-darwin20-gcc; ASMOPT=""; else VPXT=x86_64-darwin20-gcc
    if command -v nasm >/dev/null || command -v yasm >/dev/null; then ASMOPT=""; else VPXT=generic-gnu; ASMOPT=""; fi; fi
  rm -rf build/vpx && mkdir -p build/vpx && cd build/vpx
  "$ROOT/deps/libvpx/configure" --target=$VPXT --prefix="$PREFIX" --disable-examples --disable-tools --disable-docs \
     --disable-unit-tests --disable-vp9 --enable-vp8 --enable-realtime-only --enable-multithread --enable-static \
     --disable-shared --extra-cflags="-mmacosx-version-min=11.0" $ASMOPT >> "$LOG" 2>&1 || fail "libvpx (configure)"
  make -j"$JOBS" >> "$LOG" 2>&1 || fail "libvpx (compile)"
  make install >> "$LOG" 2>&1 || fail "libvpx (install)"
  cd "$ROOT"
fi

# 4. the plugin
step "b2b plugin (this is the long one, a few minutes)"
cmake -S . -B build/plugin "${COMMON[@]}" -DB2B_DEPS="$PREFIX" >> "$LOG" 2>&1 || fail "plugin (configure)"
cmake --build build/plugin >> "$LOG" 2>&1 || fail "plugin (compile)"

# 5. install into the standard plug-in folders + ad-hoc sign
step "Installing"
ART="build/plugin/SmashLink_artefacts/Release"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"
MART="build/plugin/B2BMidi_artefacts/Release"
rm -rf "$VST3_DIR/b2b.vst3" "$AU_DIR/b2b.component" "$VST3_DIR/b2b MIDI.vst3" "$AU_DIR/b2b MIDI.component"
cp -R "$ART/VST3/b2b.vst3" "$VST3_DIR/"
cp -R "$ART/AU/b2b.component" "$AU_DIR/"
cp -R "$MART/VST3/b2b MIDI.vst3" "$VST3_DIR/"
cp -R "$MART/AU/b2b MIDI.component" "$AU_DIR/"
for p in "$VST3_DIR/b2b.vst3" "$AU_DIR/b2b.component" "$VST3_DIR/b2b MIDI.vst3" "$AU_DIR/b2b MIDI.component"; do
  codesign --force --deep -s - "$p" >> "$LOG" 2>&1 || true
done
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

echo
echo "DONE."
echo "  VST3: $VST3_DIR/b2b.vst3"
echo "  AU:   $AU_DIR/b2b.component"
echo "  b2b MIDI (instrument): same folders"
echo "Rescan plugins in your DAW (Ableton: Preferences > Plug-ins > Rescan; FL: Manage plugins > Find)."
