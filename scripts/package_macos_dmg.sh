#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  ./scripts/package_macos_dmg.sh [options]

Options:
  -a, --arch <arch>           构建架构，默认使用当前机器架构。常用：arm64、x86_64、universal
  -o, --output-dir <dir>      DMG 输出目录，默认：./dist
      --app <path>            指定 OrcaSlicer.app 路径；不指定时使用 ./build/<arch>/OrcaSlicer/OrcaSlicer.app
      --name <name>           应用名/DMG 文件名前缀，默认：OrcaSlicer
      --volume <name>         DMG 挂载后的卷名，默认：OrcaSlicer
      --include-validator     同时打入 OrcaSlicer_profile_validator.app
      --sign <identity>       对 DMG 内的 app 副本做 codesign。公开分发还需要 notarize
  -h, --help                  显示帮助

Examples:
  ./build_release_macos.sh -sxa arm64 -c Release
  ./scripts/package_macos_dmg.sh -a arm64

  ./build_release_macos.sh -sxa universal -c Release
  ./scripts/package_macos_dmg.sh -a universal
EOF
}

fail() {
    echo "error: $*" >&2
    exit 1
}

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="$(uname -m)"
OUTPUT_DIR="$PROJECT_DIR/dist"
APP_NAME="OrcaSlicer"
VOLUME_NAME="OrcaSlicer"
APP_PATH=""
INCLUDE_VALIDATOR=0
SIGN_IDENTITY=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -a|--arch)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            ARCH="$2"
            shift 2
            ;;
        -o|--output-dir)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --app)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            APP_PATH="$2"
            shift 2
            ;;
        --name)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            APP_NAME="$2"
            shift 2
            ;;
        --volume)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            VOLUME_NAME="$2"
            shift 2
            ;;
        --include-validator)
            INCLUDE_VALIDATOR=1
            shift
            ;;
        --sign)
            [[ $# -ge 2 ]] || fail "$1 requires a value"
            SIGN_IDENTITY="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown option: $1"
            ;;
    esac
done

command -v hdiutil >/dev/null 2>&1 || fail "hdiutil not found. This script must run on macOS."
command -v ditto >/dev/null 2>&1 || fail "ditto not found. This script must run on macOS."

if [[ -z "$APP_PATH" ]]; then
    APP_PATH="$PROJECT_DIR/build/$ARCH/OrcaSlicer/OrcaSlicer.app"
fi

[[ -d "$APP_PATH" ]] || fail "app bundle not found: $APP_PATH"
[[ -d "$APP_PATH/Contents" ]] || fail "not a valid .app bundle: $APP_PATH"

VERSION="dev"
INFO_PLIST="$APP_PATH/Contents/Info.plist"
if [[ -f "$INFO_PLIST" ]]; then
    DETECTED_VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$INFO_PLIST" 2>/dev/null || true)"
    if [[ -n "$DETECTED_VERSION" ]]; then
        VERSION="$DETECTED_VERSION"
    fi
fi

VERSION_SAFE="$(printf '%s' "$VERSION" | tr -c 'A-Za-z0-9._-' '-')"
ARCH_SAFE="$(printf '%s' "$ARCH" | tr -c 'A-Za-z0-9._-' '-')"

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
DMG_PATH="$OUTPUT_DIR/${APP_NAME}-mac-${ARCH_SAFE}-${VERSION_SAFE}.dmg"

STAGE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/${APP_NAME}-dmg.XXXXXX")"
cleanup() {
    rm -rf "$STAGE_ROOT"
}
trap cleanup EXIT

STAGE_DIR="$STAGE_ROOT/$APP_NAME"
mkdir -p "$STAGE_DIR"

echo "Project: $PROJECT_DIR"
echo "App:     $APP_PATH"
echo "Version: $VERSION"
echo "Arch:    $ARCH"
echo "Output:  $DMG_PATH"
echo

echo "Copying app bundle..."
ditto "$APP_PATH" "$STAGE_DIR/${APP_NAME}.app"

VALIDATOR_PATH="$(dirname "$APP_PATH")/OrcaSlicer_profile_validator.app"
if [[ "$INCLUDE_VALIDATOR" -eq 1 ]]; then
    if [[ -d "$VALIDATOR_PATH" ]]; then
        echo "Copying profile validator..."
        ditto "$VALIDATOR_PATH" "$STAGE_DIR/OrcaSlicer_profile_validator.app"
    else
        echo "warning: --include-validator was set, but validator app was not found: $VALIDATOR_PATH" >&2
    fi
fi

ln -s /Applications "$STAGE_DIR/Applications"

if [[ -n "$SIGN_IDENTITY" ]]; then
    command -v codesign >/dev/null 2>&1 || fail "codesign not found"
    echo "Signing staged app with identity: $SIGN_IDENTITY"
    codesign --force --deep --options runtime --timestamp --sign "$SIGN_IDENTITY" "$STAGE_DIR/${APP_NAME}.app"
fi

echo "Creating DMG..."
hdiutil create \
    -volname "$VOLUME_NAME" \
    -srcfolder "$STAGE_DIR" \
    -ov \
    -format UDZO \
    "$DMG_PATH"

echo
echo "Done:"
echo "  $DMG_PATH"
echo
echo "Test it with:"
echo "  open \"$DMG_PATH\""

if [[ -z "$SIGN_IDENTITY" ]]; then
    echo
    echo "Note: this DMG is not signed or notarized. It is fine for local testing,"
    echo "but public distribution needs Developer ID signing and Apple notarization."
fi
