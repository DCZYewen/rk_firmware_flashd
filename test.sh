#!/bin/bash
# test.sh — Build and run all tests
# Usage: bash test.sh
set -e

# ============================================================
# Build options — change these to suit your needs
# ============================================================
ENABLE_RCE=OFF      # AT+EXEC remote command (OFF = disabled)
ENABLE_REBOOT=ON    # AT+REBOOT (ON = enabled)
# ============================================================

BUILD_DIR="build"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$SCRIPT_DIR"

echo "=== Building tests (RCE=${ENABLE_RCE}, REBOOT=${ENABLE_REBOOT}) ==="
rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
cmake -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTS=ON \
  -DENABLE_RCE="$ENABLE_RCE" \
  -DENABLE_REBOOT="$ENABLE_REBOOT"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ""
echo "=== Running tests ==="
cd "$BUILD_DIR"
ctest --output-on-failure
