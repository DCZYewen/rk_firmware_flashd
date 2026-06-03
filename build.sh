#!/bin/bash
# build.sh — Build the project natively
# Usage: bash build.sh [Debug|Release]
set -e

BUILD_TYPE="${1:-Debug}"
BUILD_DIR="build"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$SCRIPT_DIR"

echo "=== Building rk_firmware_flashd (${BUILD_TYPE}) ==="
rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ""
echo "=== Build complete: ${BUILD_DIR}/rk_firmware_flashd ==="
file "$BUILD_DIR/rk_firmware_flashd"
