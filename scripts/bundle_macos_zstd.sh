#!/usr/bin/env bash
# Bundle the executable's external zstd dependency in a staged app only.
set -euo pipefail

fail() { echo "error: $*" >&2; exit 1; }

[[ $# -eq 1 || ( $# -eq 3 && "$2" == "--build-runtime" ) ]] ||
    fail "usage: bash bundle_macos_zstd.sh <staged.app> [--build-runtime <work directory>]"
[[ "$(uname -s)" == Darwin ]] || fail "This script must run on macOS."
APP_PATH="$1"
RUNTIME_BUILD_DIR="${3:-}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXECUTABLE_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP_PATH/Contents/Info.plist")"
[[ -n "$EXECUTABLE_NAME" && "$EXECUTABLE_NAME" != */* && "$EXECUTABLE_NAME" != .* ]] ||
    fail "invalid CFBundleExecutable in $APP_PATH"
EXECUTABLE="$APP_PATH/Contents/MacOS/$EXECUTABLE_NAME"
[[ -f "$EXECUTABLE" ]] || fail "executable not found: $EXECUTABLE"

dependencies() {
    otool -L "$1" | sed -n 's/^[[:space:]]*\(.*\) (compatibility version.*$/\1/p' | sort -u
}

minimum_macos() {
    local version
    version="$(otool -arch "$2" -l "$1" | awk '
        $1 == "cmd" { command = $2 }
        command == "LC_BUILD_VERSION" && $1 == "minos" { print $2 }
        command == "LC_VERSION_MIN_MACOSX" && $1 == "version" { print $2 }
    ')"
    [[ "$version" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]] || fail "cannot determine minimum macOS: $1 ($2)"
    printf '%s\n' "$version"
}

version_le() {
    awk -v lhs="$1" -v rhs="$2" 'BEGIN {
        split(lhs, a, "."); split(rhs, b, ".");
        for (i=1; i<=3; i++) {
            if (a[i]+0 < b[i]+0) exit 0;
            if (a[i]+0 > b[i]+0) exit 1;
        }
        exit 0;
    }'
}

validate_runtime() {
    local library="$1" architecture executable_min library_min
    [[ -f "$library" ]] || fail "zstd library not found: $library"
    for architecture in $ARCHITECTURES; do
        lipo "$library" -verify_arch "$architecture" || fail "zstd library lacks $architecture: $library"
        executable_min="$(minimum_macos "$EXECUTABLE" "$architecture")"
        library_min="$(minimum_macos "$library" "$architecture")"
        version_le "$library_min" "$executable_min" ||
            fail "zstd requires macOS $library_min but app targets $executable_min ($architecture); rebuild the runtime"
        echo "Runtime compatibility: $architecture, zstd macOS $library_min <= app macOS $executable_min"
    done
}

# Capture before the loop so failures inspecting a binary cannot be swallowed
# by process substitution. Universal binaries may list the same library twice.
DEPENDENCIES="$(dependencies "$EXECUTABLE")"
ARCHITECTURES="$(lipo -archs "$EXECUTABLE")"
BUILT_RUNTIME=""
CHANGED=0
while IFS= read -r dependency; do
    case "$dependency" in
        /System/Library/*|/usr/lib/*|@*) continue ;;
        /*/libzstd*.dylib)
            LIBRARY_SOURCE="$dependency"
            if [[ -n "$RUNTIME_BUILD_DIR" ]]; then
                if [[ -z "$BUILT_RUNTIME" ]]; then
                    DEPLOYMENT_TARGET=""
                    for architecture in $ARCHITECTURES; do
                        current_min="$(minimum_macos "$EXECUTABLE" "$architecture")"
                        if [[ -z "$DEPLOYMENT_TARGET" ]] || version_le "$current_min" "$DEPLOYMENT_TARGET"; then
                            DEPLOYMENT_TARGET="$current_min"
                        fi
                    done
                    BUILD_DIR="$RUNTIME_BUILD_DIR/${ARCHITECTURES// /-}-$DEPLOYMENT_TARGET"
                    bash "$SCRIPT_DIR/build_macos_zstd.sh" "$ARCHITECTURES" "$DEPLOYMENT_TARGET" "$BUILD_DIR"
                    BUILT_RUNTIME="$BUILD_DIR/install/lib/libzstd.1.dylib"
                    mkdir -p "$APP_PATH/Contents/Resources/licenses"
                    cp "$BUILD_DIR/install/zstd-LICENSE.txt" "$APP_PATH/Contents/Resources/licenses/zstd-LICENSE.txt"
                fi
                LIBRARY_SOURCE="$BUILT_RUNTIME"
            fi
            validate_runtime "$LIBRARY_SOURCE"

            # zstd should be self-contained apart from macOS system libraries.
            # Refuse a custom build with more unbundled dependencies.
            LIBRARY_ID="$(otool -D "$LIBRARY_SOURCE" | sed -n '2p')"
            LIBRARY_DEPENDENCIES="$(dependencies "$LIBRARY_SOURCE")"
            while IFS= read -r nested; do
                [[ "$nested" == "$LIBRARY_ID" ]] && continue
                case "$nested" in
                    /System/Library/*|/usr/lib/*|'') ;;
                    *) fail "zstd has an unsupported external dependency: $nested" ;;
                esac
            done <<< "$LIBRARY_DEPENDENCIES"

            LIBRARY_NAME="$(basename "$dependency")"
            FRAMEWORKS="$APP_PATH/Contents/Frameworks"
            TARGET="$FRAMEWORKS/$LIBRARY_NAME"
            mkdir -p "$FRAMEWORKS"
            [[ ! -e "$TARGET" ]] || fail "refusing to overwrite existing bundled library: $TARGET"
            # -L dereferences Homebrew's symlink into a real, portable file.
            cp -L "$LIBRARY_SOURCE" "$TARGET"
            chmod u+w "$TARGET"
            install_name_tool -id "@rpath/$LIBRARY_NAME" "$TARGET"
            install_name_tool -change "$dependency" "@executable_path/../Frameworks/$LIBRARY_NAME" "$EXECUTABLE"
            # Refresh only the modified Mach-O's ad-hoc signature. This is not
            # Developer ID signing, app-bundle signing, or notarization.
            codesign --force --sign - "$TARGET"
            echo "Bundled zstd: $LIBRARY_SOURCE -> Contents/Frameworks/$LIBRARY_NAME"
            CHANGED=1
            ;;
        /*) fail "executable has another unbundled dependency: $dependency" ;;
    esac
done <<< "$DEPENDENCIES"

if [[ "$CHANGED" -eq 1 ]]; then
    codesign --force --sign - "$EXECUTABLE"
fi

DEPENDENCIES="$(dependencies "$EXECUTABLE")"
while IFS= read -r dependency; do
    case "$dependency" in
        @executable_path/../Frameworks/libzstd*.dylib)
            validate_runtime "$APP_PATH/Contents/Frameworks/$(basename "$dependency")" ;;
        /System/Library/*|/usr/lib/*|@*|'') ;;
        *) fail "executable still depends on an external path: $dependency" ;;
    esac
done <<< "$DEPENDENCIES"
