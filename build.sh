#!/bin/bash
# build.sh — Build natively or cross-compile
# Usage:
#   bash build.sh                         # Debug native build
#   bash build.sh Release                 # Release native build
#   bash build.sh clean                   # Remove all build directories
#   bash build.sh -allow-rce              # Enable AT+EXEC (default: OFF)
#   bash build.sh --allow-reboot          # Enable AT+REBOOT (default: ON)
#   bash build.sh --cross-toolchain=PATH  # Cross-compile with toolchain
#
# Examples:
#   bash build.sh clean
#   bash build.sh -allow-rce --allow-reboot
#   bash build.sh --cross-toolchain=/opt/toolchains/aarch64-none-linux-gnu
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Defaults
BUILD_TYPE="Debug"
DO_CLEAN=false
ENABLE_RCE=OFF
ENABLE_REBOOT=ON
CROSS_TOOLCHAIN=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        clean)
            DO_CLEAN=true
            ;;
        Debug|Release)
            BUILD_TYPE="$1"
            ;;
        -allow-rce|--allow-rce)
            ENABLE_RCE=ON
            ;;
        --allow-reboot)
            ENABLE_REBOOT=ON
            ;;
        --disable-reboot)
            ENABLE_REBOOT=OFF
            ;;
        --cross-toolchain=*)
            CROSS_TOOLCHAIN="${1#*=}"
            CROSS_TOOLCHAIN="${CROSS_TOOLCHAIN/#\~/$HOME}"
            ;;
        --cross-toolchain)
            shift
            CROSS_TOOLCHAIN="$1"
            CROSS_TOOLCHAIN="${CROSS_TOOLCHAIN/#\~/$HOME}"
            ;;
        --compile-commands)
            # Just symlink compile_commands.json for clangd without building.
            # CMAKE_EXPORT_COMPILE_COMMANDS is ON by default in CMakeLists.txt.
            if [ -f "build/compile_commands.json" ]; then
                ln -sf build/compile_commands.json compile_commands.json
                echo "=== Symlinked compile_commands.json (native build) ==="
                exit 0
            elif [ -f "build_cross/compile_commands.json" ]; then
                ln -sf build_cross/compile_commands.json compile_commands.json
                echo "=== Symlinked compile_commands.json (cross build) ==="
                exit 0
            else
                echo "No existing compile_commands.json found — run a build first."
                exit 1
            fi
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [clean] [Debug|Release] [-allow-rce] [--allow-reboot] [--cross-toolchain=PATH] [--compile-commands]"
            exit 1
            ;;
    esac
    shift
done

# Clean
if $DO_CLEAN; then
    echo "=== Cleaning build directories ==="
    rm -rf build build_cross
    exit 0
fi

# Assemble cmake arguments
CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DENABLE_RCE="$ENABLE_RCE"
    -DENABLE_REBOOT="$ENABLE_REBOOT"
)

TOOLCHAIN_CMAKE=""
if [[ -n "$CROSS_TOOLCHAIN" ]]; then
    # Detect which cmake toolchain file to use
    if [[ -f "$CROSS_TOOLCHAIN/bin/aarch64-none-linux-gnu-gcc" ]]; then
        TOOLCHAIN_CMAKE="cmake/toolchain/aarch64-linux-gnu.cmake"
    elif [[ -f "$CROSS_TOOLCHAIN/bin/arm-linux-gnueabihf-gcc" ]]; then
        TOOLCHAIN_CMAKE="cmake/toolchain/arm-linux-gnueabihf.cmake"
    else
        echo "ERROR: could not detect toolchain architecture at '$CROSS_TOOLCHAIN'"
        echo "Expected aarch64-none-linux-gnu-gcc or arm-linux-gnueabihf-gcc"
        exit 1
    fi
fi

if [[ -n "$TOOLCHAIN_CMAKE" ]]; then
    BUILD_DIR="build_cross"
    CMAKE_ARGS+=(
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_CMAKE"
        -DTOOLCHAIN_ROOT="$CROSS_TOOLCHAIN"
    )
    echo "=== Cross-compiling: ${BUILD_TYPE} using ${TOOLCHAIN_CMAKE} ==="
else
    BUILD_DIR="build"
    echo "=== Native build: ${BUILD_TYPE} ==="
fi

echo "    RCE=${ENABLE_RCE}, REBOOT=${ENABLE_REBOOT}"
cmake -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$(nproc)"

BINARY="$BUILD_DIR/rk_firmware_flashd"

# Strip release binary
if [[ "$BUILD_TYPE" == "Release" ]]; then
    if command -v strip &>/dev/null && [[ -f "$BINARY" ]]; then
        strip "$BINARY"
        echo "=== Stripped release binary ==="
    fi
fi

# Symlink compile_commands.json for clangd
if [ -f "$BUILD_DIR/compile_commands.json" ]; then
    ln -sf "$BUILD_DIR/compile_commands.json" compile_commands.json
    echo "=== Symlinked compile_commands.json for clangd ==="
fi

echo ""
echo "=== Build complete: ${BUILD_DIR}/rk_firmware_flashd ==="
file "$BINARY"
