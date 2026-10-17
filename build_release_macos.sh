#!/bin/bash

set -e
set -o pipefail

while getopts ":dpa:snt:xbc:1h" opt; do
  case "${opt}" in
    d )
        export BUILD_TARGET="deps"
        ;;
    p )
        export PACK_DEPS="1"
        ;;
    a )
        export ARCH="$OPTARG"
        ;;
    s )
        export BUILD_TARGET="slicer"
        ;;
    n )
        export NIGHTLY_BUILD="1"
        ;;
    t )
        export OSX_DEPLOYMENT_TARGET="$OPTARG"
        ;;
    x )
        export SLICER_CMAKE_GENERATOR="Ninja Multi-Config"
        export SLICER_BUILD_TARGET="all"
        export DEPS_CMAKE_GENERATOR="Ninja"
        ;;
    b )
        export BUILD_ONLY="1"
        ;;
    c )
        export BUILD_CONFIG="$OPTARG"
        ;;
    1 )
        export CMAKE_BUILD_PARALLEL_LEVEL=1
        ;;
    h ) echo "Usage: ./build_release_macos.sh [-d]"
        echo "   -d: Build deps only"
        echo "   -a: Set ARCHITECTURE (arm64 or x86_64 or universal)"
        echo "   -s: Build slicer only"
        echo "   -n: Nightly build"
        echo "   -t: Minimum macOS version (11.3 normally; at least 14.0 with cached MeshLib ON)"
        echo "   -x: Use Ninja Multi-Config CMake generator, default is Xcode"
        echo "   -b: Build without reconfiguring CMake"
        echo "   -c: Set CMake build configuration, default is Release"
        echo "   -1: Use single job for building"
        exit 0
        ;;
    * )
        ;;
  esac
done

# Set defaults

if [ -z "$ARCH" ]; then
    ARCH="$(uname -m)"
    export ARCH
fi

if [ -z "$BUILD_CONFIG" ]; then
  export BUILD_CONFIG="Release"
fi

if [ -z "$BUILD_TARGET" ]; then
  export BUILD_TARGET="all"
fi

if [ -z "$SLICER_CMAKE_GENERATOR" ]; then
  export SLICER_CMAKE_GENERATOR="Xcode"
fi

if [ -z "$SLICER_BUILD_TARGET" ]; then
  export SLICER_BUILD_TARGET="ALL_BUILD"
fi

if [ -z "$DEPS_CMAKE_GENERATOR" ]; then
  export DEPS_CMAKE_GENERATOR="Unix Makefiles"
fi

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

function cached_value() {
    local cache="$PROJECT_DIR/build/$ARCH/CMakeCache.txt"
    if [ -f "$cache" ]; then
        sed -n "s/^$1:[^=]*=//p" "$cache" | head -n 1
    fi
}

MESHLIB_ENABLED="$(cached_value ORCA_MESHLIB_LOCAL_EVALUATION | tr '[:lower:]' '[:upper:]')"
case "$MESHLIB_ENABLED" in
    ON|TRUE|YES|1) MESHLIB_ENABLED=ON ;;
    *) MESHLIB_ENABLED=OFF ;;
esac
if [ "$MESHLIB_ENABLED" = ON ] && [ "$BUILD_TARGET" != deps ]; then
    if [ "$ARCH" != arm64 ]; then
        echo "error: MeshLib packaging currently supports arm64 only" >&2
        exit 1
    fi
    if [ -z "$OSX_DEPLOYMENT_TARGET" ]; then
        OSX_DEPLOYMENT_TARGET="$(cached_value CMAKE_OSX_DEPLOYMENT_TARGET)"
        OSX_DEPLOYMENT_TARGET="$(python3 - "$OSX_DEPLOYMENT_TARGET" <<'PY'
import re, sys
value = sys.argv[1]
print(value if re.fullmatch(r'\d+(\.\d+){0,2}', value) and int(value.split('.')[0]) >= 14 else '14.0')
PY
)"
    fi
    python3 - "$OSX_DEPLOYMENT_TARGET" <<'PY'
import re, sys
value = sys.argv[1]
if not re.fullmatch(r'\d+(\.\d+){0,2}', value) or int(value.split('.')[0]) < 14:
    sys.exit('error: MeshLib requires macOS 14.0+; use -t 14.0 (or newer)')
PY
fi
export OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET:-11.3}"

echo "Build params:"
echo " - ARCH: $ARCH"
echo " - BUILD_CONFIG: $BUILD_CONFIG"
echo " - BUILD_TARGET: $BUILD_TARGET"
echo " - CMAKE_GENERATOR: $SLICER_CMAKE_GENERATOR for Slicer, $DEPS_CMAKE_GENERATOR for deps"
echo " - OSX_DEPLOYMENT_TARGET: $OSX_DEPLOYMENT_TARGET"
echo

# if which -s brew; then
# 	brew --prefix libiconv
# 	brew --prefix zstd
# 	export LIBRARY_PATH=$LIBRARY_PATH:$(brew --prefix zstd)/lib/
# elif which -s port; then
# 	port install libiconv
# 	port install zstd
# 	export LIBRARY_PATH=$LIBRARY_PATH:/opt/local/lib
# else
# 	echo "Need either brew or macports to successfully build deps"
# 	exit 1
# fi

PROJECT_BUILD_DIR="$PROJECT_DIR/build/$ARCH"
DEPS_DIR="$PROJECT_DIR/deps"
DEPS_BUILD_DIR="$DEPS_DIR/build/$ARCH"
DEPS="$DEPS_BUILD_DIR/OrcaSlicer_deps"

# For Multi-config generators like Ninja and Xcode
export BUILD_DIR_CONFIG_SUBDIR="/$BUILD_CONFIG"

function build_deps() {
    # iterate over two architectures: x86_64 and arm64
    for _ARCH in x86_64 arm64; do
        # if ARCH is universal or equal to _ARCH
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then

            PROJECT_BUILD_DIR="$PROJECT_DIR/build/$_ARCH"
            DEPS_BUILD_DIR="$DEPS_DIR/build/$_ARCH"
            DEPS="$DEPS_BUILD_DIR/OrcaSlicer_dep"

            echo "Building deps..."
            (
                set -x
                mkdir -p "$DEPS"
                cd "$DEPS_BUILD_DIR"
                if [ "1." != "$BUILD_ONLY". ]; then
                    cmake "${DEPS_DIR}" \
                        -G "${DEPS_CMAKE_GENERATOR}" \
                        -DDESTDIR="$DEPS" \
                        -DOPENSSL_ARCH="darwin64-${_ARCH}-cc" \
                        -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                        -DCMAKE_OSX_ARCHITECTURES:STRING="${_ARCH}" \
                        -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}"
                fi
                cmake --build . --config "$BUILD_CONFIG" --target deps
            )
        fi
    done
}

function pack_deps() {
    echo "Packing deps..."
    (
        set -x
        cd "$DEPS_DIR"
        tar -zcvf "OrcaSlicer_dep_mac_${ARCH}_$(date +"%Y%m%d").tar.gz" "build"
    )
}

function build_slicer() {
    local meshlib_args=()
    if [ "$MESHLIB_ENABLED" = ON ]; then
        MESHLIB_RUNTIME_DIR="$PROJECT_DIR/build/$ARCH/meshlib-runtime/MeshLib"
        if [ "$BUILD_ONLY" = 1 ] && {
            [ "$(cached_value ORCA_MESHLIB_RUNTIME_DIR)" != "$MESHLIB_RUNTIME_DIR" ] ||
            [ "$(cached_value CMAKE_OSX_DEPLOYMENT_TARGET)" != "$OSX_DEPLOYMENT_TARGET" ];
        }; then
            echo "error: cached MeshLib runtime/target is not prepared for packaging; rerun without -b" >&2
            exit 1
        fi
        python3 "$PROJECT_DIR/scripts/macos_runtime_bundle.py" prepare-meshlib \
            --sdk "$(cached_value ORCA_MESHLIB_SDK_ROOT)" \
            --output "$MESHLIB_RUNTIME_DIR" --minimum "$OSX_DEPLOYMENT_TARGET"
        meshlib_args=(-DORCA_MESHLIB_RUNTIME_DIR:PATH="$MESHLIB_RUNTIME_DIR")
    fi
    # Boost iostreams may otherwise discover a Homebrew runtime requiring a
    # newer OS than the app. Use one pinned prefix for all slices of universal.
    local runtime_architectures="$ARCH"
    if [ "$ARCH" = "universal" ]; then
        runtime_architectures="x86_64 arm64"
    fi
    MACOS_ZSTD_DIR="$PROJECT_DIR/build/$ARCH/macos-zstd/$OSX_DEPLOYMENT_TARGET"
    bash "$PROJECT_DIR/scripts/build_macos_zstd.sh" "$runtime_architectures" "$OSX_DEPLOYMENT_TARGET" "$MACOS_ZSTD_DIR"
    local zstd_config_dir="$MACOS_ZSTD_DIR/install/lib/cmake/zstd"

    # iterate over two architectures: x86_64 and arm64
    for _ARCH in x86_64 arm64; do
        # if ARCH is universal or equal to _ARCH
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then

            PROJECT_BUILD_DIR="$PROJECT_DIR/build/$_ARCH"
            DEPS_BUILD_DIR="$DEPS_DIR/build/$_ARCH"
            DEPS="$DEPS_BUILD_DIR/OrcaSlicer_dep"

            echo "Building slicer for $_ARCH..."
            (
                set -x
            mkdir -p "$PROJECT_BUILD_DIR"
            cd "$PROJECT_BUILD_DIR"
            if [ "1." != "$BUILD_ONLY". ]; then
                cmake "${PROJECT_DIR}" \
                    -G "${SLICER_CMAKE_GENERATOR}" \
                    -DBBL_RELEASE_TO_PUBLIC=1 \
                    -DORCA_TOOLS=ON \
                    "${meshlib_args[@]}" \
                    ${ORCA_UPDATER_SIG_KEY:+-DORCA_UPDATER_SIG_KEY="$ORCA_UPDATER_SIG_KEY"} \
                    -DCMAKE_PREFIX_PATH="$DEPS/usr/local" \
                    -Dzstd_DIR:PATH="$zstd_config_dir" \
                    -DCMAKE_INSTALL_PREFIX="$PWD/OrcaSlicer" \
                    -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                    -DCMAKE_MACOSX_RPATH=ON \
                    -DCMAKE_INSTALL_RPATH="${DEPS}/usr/local" \
                    -DCMAKE_MACOSX_BUNDLE=ON \
                    -DCMAKE_OSX_ARCHITECTURES="${_ARCH}" \
                    -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}"
            elif ! grep -Fqx "zstd_DIR:PATH=$zstd_config_dir" CMakeCache.txt; then
                echo "error: cached zstd does not match the portable runtime; rerun without -b" >&2
                exit 1
            fi
            cmake --build . --config "$BUILD_CONFIG" --target "$SLICER_BUILD_TARGET"
        )

        echo "Verify localization with gettext..."
        (
            cd "$PROJECT_DIR"
            ./scripts/run_gettext.sh
        )

        echo "Fix macOS app package..."
        (
            cd "$PROJECT_BUILD_DIR"
            mkdir -p OrcaSlicer
            cd OrcaSlicer
            # remove previously built app
            rm -rf ./OrcaSlicer.app
            # fully copy newly built app
            cp -pR "../src$BUILD_DIR_CONFIG_SUBDIR/OrcaSlicer.app" ./OrcaSlicer.app
            finalize_macos_app "$PWD/OrcaSlicer.app"
            
            # Copy OrcaSlicer_profile_validator.app if it exists
            if [ -f "../src$BUILD_DIR_CONFIG_SUBDIR/OrcaSlicer_profile_validator.app/Contents/MacOS/OrcaSlicer_profile_validator" ]; then
                echo "Copying OrcaSlicer_profile_validator.app..."
                rm -rf ./OrcaSlicer_profile_validator.app
                cp -pR "../src$BUILD_DIR_CONFIG_SUBDIR/OrcaSlicer_profile_validator.app" ./OrcaSlicer_profile_validator.app
                finalize_macos_app "$PWD/OrcaSlicer_profile_validator.app"
            fi
        )

        # extract version
        # export ver=$(grep '^#define SoftFever_VERSION' ../src/libslic3r/libslic3r_version.h | cut -d ' ' -f3)
        # ver="_V${ver//\"}"
        # echo $PWD
        # if [ "1." != "$NIGHTLY_BUILD". ];
        # then
        #     ver=${ver}_dev
        # fi

        # zip -FSr OrcaSlicer${ver}_Mac_${_ARCH}.zip OrcaSlicer.app

    fi
    done
}

function finalize_macos_app() {
    local app="$1"
    # A generated Resources symlink must not let license copying/signing mutate
    # repository resources. The app copy is the only writable packaging target.
    if [ -L "$app/Contents/Resources" ]; then
        local resources_path
        resources_path="$(readlink "$app/Contents/Resources")"
        if [[ "$resources_path" != /* ]]; then
            resources_path="$app/Contents/$resources_path"
        fi
        local resources_stage
        resources_stage="$(mktemp -d "$app/Contents/Resources.XXXXXX")"
        cp -R "$resources_path/." "$resources_stage/"
        # mktemp defaults to 0700, which would hide resources from other users.
        chmod 755 "$resources_stage"
        rm "$app/Contents/Resources"
        mv "$resources_stage" "$app/Contents/Resources"
    fi
    find "$app/" -name '.DS_Store' -delete
    bash "$PROJECT_DIR/scripts/bundle_macos_zstd.sh" "$app"
    mkdir -p "$app/Contents/Resources/licenses"
    cp "$MACOS_ZSTD_DIR/install/zstd-LICENSE.txt" "$app/Contents/Resources/licenses/zstd-LICENSE.txt"
    if [ "$MESHLIB_ENABLED" = ON ]; then
        python3 "$PROJECT_DIR/scripts/macos_runtime_bundle.py" bundle-meshlib \
            --app "$app" --stage "$MESHLIB_RUNTIME_DIR"
    fi
    python3 "$PROJECT_DIR/scripts/macos_runtime_bundle.py" finalize --app "$app"
}

function build_universal() {
    echo "Building universal binary..."

    PROJECT_BUILD_DIR="$PROJECT_DIR/build/$ARCH"
    
    # Create universal binary
    echo "Creating universal binary..."
    # PROJECT_BUILD_DIR="$PROJECT_DIR/build_Universal"
    mkdir -p "$PROJECT_BUILD_DIR/OrcaSlicer"
    UNIVERSAL_APP="$PROJECT_BUILD_DIR/OrcaSlicer/OrcaSlicer.app"
    rm -rf "$UNIVERSAL_APP"
    cp -R "$PROJECT_DIR/build/arm64/OrcaSlicer/OrcaSlicer.app" "$UNIVERSAL_APP"
    
    # Get the binary path inside the .app bundle
    BINARY_PATH="Contents/MacOS/OrcaSlicer"
    
    # Create universal binary using lipo
    lipo -create \
        "$PROJECT_DIR/build/x86_64/OrcaSlicer/OrcaSlicer.app/$BINARY_PATH" \
        "$PROJECT_DIR/build/arm64/OrcaSlicer/OrcaSlicer.app/$BINARY_PATH" \
        -output "$UNIVERSAL_APP/$BINARY_PATH"
    # The copied runtime already contains both slices. Revalidate both and
    # refresh the app signature after lipo changes the main executable.
    finalize_macos_app "$UNIVERSAL_APP"
        
    echo "Universal binary created at $UNIVERSAL_APP"
    
    # Create universal binary for profile validator if it exists
    if [ -f "$PROJECT_DIR/build/arm64/OrcaSlicer/OrcaSlicer_profile_validator.app/Contents/MacOS/OrcaSlicer_profile_validator" ] && \
       [ -f "$PROJECT_DIR/build/x86_64/OrcaSlicer/OrcaSlicer_profile_validator.app/Contents/MacOS/OrcaSlicer_profile_validator" ]; then
        echo "Creating universal binary for OrcaSlicer_profile_validator..."
        UNIVERSAL_VALIDATOR_APP="$PROJECT_BUILD_DIR/OrcaSlicer/OrcaSlicer_profile_validator.app"
        rm -rf "$UNIVERSAL_VALIDATOR_APP"
        cp -R "$PROJECT_DIR/build/arm64/OrcaSlicer/OrcaSlicer_profile_validator.app" "$UNIVERSAL_VALIDATOR_APP"
        
        # Get the binary path inside the profile validator .app bundle
        VALIDATOR_BINARY_PATH="Contents/MacOS/OrcaSlicer_profile_validator"
        
        # Create universal binary using lipo
        lipo -create \
            "$PROJECT_DIR/build/x86_64/OrcaSlicer/OrcaSlicer_profile_validator.app/$VALIDATOR_BINARY_PATH" \
            "$PROJECT_DIR/build/arm64/OrcaSlicer/OrcaSlicer_profile_validator.app/$VALIDATOR_BINARY_PATH" \
            -output "$UNIVERSAL_VALIDATOR_APP/$VALIDATOR_BINARY_PATH"
        finalize_macos_app "$UNIVERSAL_VALIDATOR_APP"
            
        echo "Universal binary for OrcaSlicer_profile_validator created at $UNIVERSAL_VALIDATOR_APP"
    fi
}

case "${BUILD_TARGET}" in
    all)
        build_deps
        build_slicer
        ;;
    deps)
        build_deps
        ;;
    slicer)
        build_slicer
        ;;
    *)
        echo "Unknown target: $BUILD_TARGET. Available targets: deps, slicer, all."
        exit 1
        ;;
esac

if [ "$ARCH" = "universal" ] && [ "$BUILD_TARGET" != "deps" ]; then
    build_universal
fi

if [ "1." == "$PACK_DEPS". ]; then
    pack_deps
fi
