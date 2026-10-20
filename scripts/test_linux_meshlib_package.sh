#!/usr/bin/env bash
# Validate the final AppImage, not an executable still using the build SDK.
# Usage: bash scripts/test_linux_meshlib_package.sh APPIMAGE NEW_OUTPUT_DIR
# The caller must make the build SDK unavailable for the duration of this test.
set -euo pipefail

if [[ $# != 2 || $(uname -s) != Linux ]]; then
    echo "Usage (Linux): $0 APPIMAGE NEW_OUTPUT_DIR" >&2
    exit 2
fi
scripts_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
appimage=$(realpath -e -- "$1")
output=$(realpath -m -- "$2")
if [[ ! -x "$appimage" || -e "$output" || -L "$output" ]]; then
    echo "AppImage must be executable; the output directory must not exist." >&2
    exit 2
fi

mkdir -- "$output"
mkdir -- "$output/extract" "$output/relocated"
unset LD_LIBRARY_PATH LD_PRELOAD LD_AUDIT
cd -- "$output/extract"
"$appimage" --appimage-extract > "$output/extract.txt" 2>&1
mv -- squashfs-root "$output/relocated/OrcaSlicer"
package="$output/relocated/OrcaSlicer"
runtime="$package/lib/meshlib"
binary="$package/bin/orca-slicer"
cd -- "$output"

python3 "$scripts_dir/package_meshlib_linux.py" verify --package "$package" \
    > "$output/runtime-verification.txt" 2>&1
# The server invokes bin/orca-slicer directly. Verify its relative RPATH without
# an LD_LIBRARY_PATH workaround, as well as the same-PID/no-worker assertions.
python3 "$scripts_dir/test_cli_meshlib_reconstruction.py" \
    --binary "$binary" --output-dir "$output/meshlib" --timeout 180
python3 "$scripts_dir/test_cli_model_stage_report.py" \
    --binary "$binary" --output-dir "$output/model-stage" --meshlib enabled --timeout 180

# Independently check that the distributed launcher supplies its own runtime
# path, including an actual reconstruction rather than just --help.
timeout 30s "$package/AppRun" --help > "$output/apprun-help.txt" 2>&1
timeout 180s "$package/AppRun" --reconstruct-model "$output/meshlib/fixtures/cube.stl" \
    --reconstruction-engine meshlib --reconstruction-voxel-mm 0.0803 \
    --accept-reconstruction-risk --outputdir "$output/apprun-candidate" \
    > "$output/apprun-reconstruction.txt" 2>&1
jq --exit-status --arg runtime "$runtime/" \
    '.status == "candidate_ready" and .mesh_validated == true and
     .export_round_trip == "passed" and .engine.sdk.runtime_identity_verified == true and
     (.engine.sdk.runtime_libraries | length == 2) and
     all(.engine.sdk.runtime_libraries[]; .path | startswith($runtime))' \
    "$output/apprun-candidate/reconstruction.json"
echo "Linux MeshLib package verification passed. Evidence: $output"
