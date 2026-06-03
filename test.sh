#!/bin/bash
# test.sh — Build and run all tests
# Usage: bash test.sh
set -e

BUILD_DIR="build"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$SCRIPT_DIR"

echo "=== Building tests ==="
rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ""
echo "=== Running tests ==="
cd "$BUILD_DIR"
ctest --output-on-failure
