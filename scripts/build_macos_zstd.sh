#!/usr/bin/env bash
# Build a pinned, portable zstd runtime, never a Homebrew bottle.
set -euo pipefail
fail() { echo "error: $*" >&2; exit 1; }
[[ $# -eq 3 ]] || fail "usage: bash build_macos_zstd.sh <space-separated architectures> <minimum macOS> <work directory>"
[[ "$(uname -s)" == Darwin ]] || fail "This script must run on macOS."
ARCHITECTURES="$1"
DEPLOYMENT_TARGET="$2"
WORK_DIR="$3"
[[ "$DEPLOYMENT_TARGET" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]] || fail "invalid macOS target"
[[ -n "$ARCHITECTURES" ]] || fail "missing architecture"
for architecture in $ARCHITECTURES; do
    case "$architecture" in arm64|x86_64) ;; *) fail "unsupported architecture: $architecture" ;; esac
done

ZSTD_VERSION=1.5.7
ZSTD_SHA256=37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3
mkdir -p "$WORK_DIR"
WORK_DIR="$(cd "$WORK_DIR" && pwd)"
ARCHIVE="${WISEBEGINNER_ZSTD_ARCHIVE:-$WORK_DIR/zstd-$ZSTD_VERSION.tar.gz}"
if [[ ! -f "$ARCHIVE" ]]; then
    [[ -z "${WISEBEGINNER_ZSTD_ARCHIVE:-}" ]] || fail "offline zstd archive not found: $ARCHIVE"
    curl --fail --location --retry 2 --connect-timeout 15 --max-time 180 \
        "https://codeload.github.com/facebook/zstd/tar.gz/refs/tags/v$ZSTD_VERSION" -o "$ARCHIVE"
fi
ACTUAL_SHA256="$(shasum -a 256 "$ARCHIVE" | awk '{print $1}')"
[[ "$ACTUAL_SHA256" == "$ZSTD_SHA256" ]] || fail "zstd source SHA-256 mismatch: $ARCHIVE"
SOURCE_DIR="$WORK_DIR/zstd-$ZSTD_VERSION"
# Preserve source timestamps on repeated builds so app and validator packaging
# share an incremental runtime build. Always verify the archive above first.
SOURCE_STAMP="$WORK_DIR/source.sha256"
if [[ ! -f "$SOURCE_DIR/build/cmake/CMakeLists.txt" || ! -f "$SOURCE_STAMP" ||
      "$(<"$SOURCE_STAMP")" != "$ZSTD_SHA256" ]]; then
    tar -xzf "$ARCHIVE" -C "$WORK_DIR"
    printf '%s\n' "$ZSTD_SHA256" > "$SOURCE_STAMP"
fi
CMAKE_ARCHITECTURES="${ARCHITECTURES// /;}"
echo "Building zstd $ZSTD_VERSION for $ARCHITECTURES, minimum macOS $DEPLOYMENT_TARGET"
cmake -S "$SOURCE_DIR/build/cmake" -B "$WORK_DIR/build" -G 'Unix Makefiles' \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$(xcrun --find clang)" \
    -DCMAKE_CXX_COMPILER="$(xcrun --find clang++)" \
    -DCMAKE_OSX_ARCHITECTURES="$CMAKE_ARCHITECTURES" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" \
    -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
    -DCMAKE_C_FLAGS= -DCMAKE_CXX_FLAGS= -DCMAKE_SHARED_LINKER_FLAGS= \
    -DCMAKE_INSTALL_PREFIX="$WORK_DIR/install" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_INSTALL_NAME_DIR="$WORK_DIR/install/lib" \
    -DCMAKE_BUILD_WITH_INSTALL_NAME_DIR=ON \
    -DBUILD_SHARED_LIBS=ON -DZSTD_BUILD_SHARED=ON -DZSTD_BUILD_STATIC=OFF -DZSTD_BUILD_PROGRAMS=OFF \
    -DZSTD_BUILD_TESTS=OFF -DZSTD_BUILD_CONTRIB=OFF -DZSTD_MULTITHREAD_SUPPORT=ON
cmake --build "$WORK_DIR/build" --parallel 4
cmake --install "$WORK_DIR/build"
cp "$SOURCE_DIR/LICENSE" "$WORK_DIR/install/zstd-LICENSE.txt"
