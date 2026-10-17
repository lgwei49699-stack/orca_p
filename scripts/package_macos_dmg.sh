#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  ./scripts/package_macos_dmg.sh [options]

Options:
  -a, --arch <arch>           构建架构，默认使用当前机器架构。常用：arm64、x86_64、universal
  -o, --output-dir <dir>      DMG 输出目录，默认：./dist
      --app <path>            指定 .app 路径；默认 ./build/<arch>/OrcaSlicer/<当前应用标识>.app
      --name <name>           应用名/DMG 文件名前缀，默认读取 version.inc 的 SLIC3R_APP_KEY
      --volume <name>         DMG 挂载后的卷名，默认读取 version.inc 的 SLIC3R_APP_KEY
      --include-validator     同时打入 OrcaSlicer_profile_validator.app
      --sign <identity>       对 DMG 内的 app 副本做 codesign。公开分发还需要 notarize
  -h, --help                  显示帮助

Runtime:
  打包时从固定版本源码编译 zstd，使用应用的架构和最低 macOS 版本；需要 CMake 和 Xcode 命令行工具。
  离线打包可设置 WISEBEGINNER_ZSTD_ARCHIVE 指向 zstd v1.5.7 源码压缩包（仍校验 SHA-256）。
  DMG 附带“安装与首次启动说明.txt”；不会自动清除下载隔离标记。

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
APP_BUNDLE_KEY="$(sed -n 's/^[[:space:]]*set(SLIC3R_APP_KEY "\([^"]*\)").*/\1/p' "$PROJECT_DIR/version.inc")"
case "$APP_BUNDLE_KEY" in
    OrcaSlicer|WiseBeginnerSlicer) ;;
    *) fail "unsupported SLIC3R_APP_KEY in version.inc" ;;
esac
ARCH="$(uname -m)"
OUTPUT_DIR="$PROJECT_DIR/dist"
APP_NAME="$APP_BUNDLE_KEY"
VOLUME_NAME="$APP_BUNDLE_KEY"
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

[[ "$(uname -s)" == Darwin ]] || fail "This script must run on macOS."
case "$APP_NAME" in
    ''|.|..|*/*) fail "--name must be a non-empty application filename, not a path" ;;
esac
[[ "$APP_NAME" =~ ^[A-Za-z0-9][A-Za-z0-9._\ -]*$ ]] || fail "--name contains unsupported characters"
command -v hdiutil >/dev/null 2>&1 || fail "hdiutil not found. This script must run on macOS."
command -v ditto >/dev/null 2>&1 || fail "ditto not found. This script must run on macOS."

if [[ -z "$APP_PATH" ]]; then
    APP_PATH="$PROJECT_DIR/build/$ARCH/OrcaSlicer/$APP_BUNDLE_KEY.app"
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

STAGE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/${APP_BUNDLE_KEY}-dmg.XXXXXX")"
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
echo "Bundling zstd runtime dependency..."
bash "$PROJECT_DIR/scripts/bundle_macos_zstd.sh" "$STAGE_DIR/${APP_NAME}.app" --build-runtime "$STAGE_ROOT/zstd-runtime"

VALIDATOR_PATH="$(dirname "$APP_PATH")/OrcaSlicer_profile_validator.app"
if [[ "$INCLUDE_VALIDATOR" -eq 1 ]]; then
    if [[ -d "$VALIDATOR_PATH" ]]; then
        echo "Copying profile validator..."
        ditto "$VALIDATOR_PATH" "$STAGE_DIR/OrcaSlicer_profile_validator.app"
        bash "$PROJECT_DIR/scripts/bundle_macos_zstd.sh" "$STAGE_DIR/OrcaSlicer_profile_validator.app" --build-runtime "$STAGE_ROOT/zstd-runtime"
    else
        echo "warning: --include-validator was set, but validator app was not found: $VALIDATOR_PATH" >&2
    fi
fi

ln -s /Applications "$STAGE_DIR/Applications"
APP_DISPLAY_NAME="$APP_NAME"
[[ "$APP_NAME" != WiseBeginnerSlicer ]] || APP_DISPLAY_NAME="智小白切片软件"
sed -e "s/@APP_NAME@/$APP_NAME/g" -e "s/@APP_DISPLAY_NAME@/$APP_DISPLAY_NAME/g" \
    "$PROJECT_DIR/scripts/macos-install-guide.txt" > "$STAGE_DIR/安装与首次启动说明.txt"

STAGED_APPS=("$STAGE_DIR/${APP_NAME}.app")
if [[ "$INCLUDE_VALIDATOR" -eq 1 && -d "$STAGE_DIR/OrcaSlicer_profile_validator.app" ]]; then
    STAGED_APPS+=("$STAGE_DIR/OrcaSlicer_profile_validator.app")
fi
for staged_app in "${STAGED_APPS[@]}"; do
    python3 "$PROJECT_DIR/scripts/macos_runtime_bundle.py" finalize \
        --app "$staged_app" --identity "${SIGN_IDENTITY:--}"
done

echo "Verifying staged app signatures (integrity only, not Gatekeeper approval)..."
codesign --verify --deep --strict "$STAGE_DIR/${APP_NAME}.app"
if [[ "$INCLUDE_VALIDATOR" -eq 1 && -d "$STAGE_DIR/OrcaSlicer_profile_validator.app" ]]; then
    codesign --verify --deep --strict "$STAGE_DIR/OrcaSlicer_profile_validator.app"
fi

echo "Creating DMG..."
hdiutil create \
    -volname "$VOLUME_NAME" \
    -srcfolder "$STAGE_DIR" \
    -ov \
    -format UDZO \
    "$DMG_PATH"

(cd "$OUTPUT_DIR" && shasum -a 256 "$(basename "$DMG_PATH")") > "$DMG_PATH.sha256"

echo
echo "Done:"
echo "  $DMG_PATH"
echo
echo "Test it with:"
echo "  open \"$DMG_PATH\""

if [[ -z "$SIGN_IDENTITY" ]]; then
    echo
    echo "WARNING: LOCAL TEST PACKAGE ONLY — no Developer ID signing or notarization."
    echo "Local launch/signature integrity does NOT prove Gatekeeper approval on another Mac."
else
    echo "WARNING: signing alone does NOT mean notarization or Gatekeeper approval."
fi
echo "Installation guide: 安装与首次启动说明.txt (inside the DMG)"
echo "SHA-256: $DMG_PATH.sha256"
