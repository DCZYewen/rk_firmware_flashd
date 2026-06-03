#!/bin/bash
# build.sh — Build the project natively
# Usage: bash build.sh [clean] [Debug|Release]
#   bash build.sh                 # Debug build
#   bash build.sh Release         # Release build
#   bash build.sh clean           # Remove build directory
#   bash build.sh clean Release   # Clean then Release build
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

# Parse arguments
DO_CLEAN=false
BUILD_TYPE="Debug"
for arg in "$@"; do
    case "$arg" in
        clean)   DO_CLEAN=true ;;
        Debug)   BUILD_TYPE="Debug" ;;
        Release) BUILD_TYPE="Release" ;;
    esac
done

# Clean
if $DO_CLEAN; then
    echo "=== Cleaning ${BUILD_DIR}/ ==="
    rm -rf "$BUILD_DIR"
fi

# Build
echo "=== Building rk_firmware_flashd (${BUILD_TYPE}) ==="
echo "    RCE=${ENABLE_RCE}, REBOOT=${ENABLE_REBOOT}"
cmake -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DENABLE_RCE="$ENABLE_RCE" \
  -DENABLE_REBOOT="$ENABLE_REBOOT"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# Strip Release binaries
if [ "$BUILD_TYPE" = "Release" ]; then
    BINARY="$BUILD_DIR/rk_firmware_flashd"
    if command -v strip &>/dev/null && [ -f "$BINARY" ]; then
        strip "$BINARY"
        echo "=== Stripped release binary ==="
    fi
fi

echo ""
echo "=== Build complete: ${BUILD_DIR}/rk_firmware_flashd ==="
file "$BUILD_DIR/rk_firmware_flashd"
