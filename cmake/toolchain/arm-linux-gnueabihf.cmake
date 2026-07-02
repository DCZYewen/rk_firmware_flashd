# =============================================================================
# ARMhf (arm-linux-gnueabihf) Cross-Compilation Toolchain
#
# Usage:
#   cmake -B build_armhf \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/arm-linux-gnueabihf.cmake
#
# Or override toolchain root:
#   cmake -B build_armhf \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/arm-linux-gnueabihf.cmake \
#     -DTOOLCHAIN_ROOT=/path/to/other/toolchain
#
# When linking against libraries not in the sysroot (e.g. libmicrohttpd):
#   -DMHD_LIB_DIR=/path/to/lib -DMHD_INCLUDE_DIR=/path/to/include
# =============================================================================

# Toolchain root (Buildroot default or custom)
if(NOT DEFINED TOOLCHAIN_ROOT)
    set(TOOLCHAIN_ROOT "/opt/toolchains/arm-linux-gnu-eabihf" CACHE PATH "Toolchain root")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Compiler (Buildroot names them arm-linux-gnueabihf-*)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_ROOT}/bin/arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_ROOT}/bin/arm-linux-gnueabihf-g++)

# Sysroot
if(NOT DEFINED SYSROOT)
    set(SYSROOT "${TOOLCHAIN_ROOT}/arm-buildroot-linux-gnueabihf/sysroot")
endif()

if(EXISTS "${SYSROOT}")
    set(CMAKE_SYSROOT ${SYSROOT})
    message(STATUS "Using sysroot: ${SYSROOT}")
else()
    message(WARNING "Sysroot not found at ${SYSROOT} — set -DSYSROOT= manually")
endif()

# Compiler flags for ARMv7 (adjust for your Cortex target)
set(CMAKE_C_FLAGS_INIT   "-march=armv7-a -mfpu=neon -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=neon -mfloat-abi=hard")

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config: point to target's .pc files
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR}      "${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/share/pkgconfig")
