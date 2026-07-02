# =============================================================================
# ARM64 (AArch64) Cross-Compilation Toolchain
#
# Usage:
#   cmake -B build_arm64 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/aarch64-linux-gnu.cmake \
#     -DTOOLCHAIN_ROOT=/opt/toolchains/aarch64-none-linux-gnu
#
# Or set SYSROOT separately:
#   cmake -B build_arm64 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/aarch64-linux-gnu.cmake \
#     -DTOOLCHAIN_ROOT=/opt/toolchains/aarch64-none-linux-gnu \
#     -DSYSROOT=/opt/toolchains/aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc
# =============================================================================

# Toolchain root (set via -DTOOLCHAIN_ROOT= or edit here)
if(NOT DEFINED TOOLCHAIN_ROOT)
    set(TOOLCHAIN_ROOT "/opt/toolchains/aarch64-none-linux-gnu" CACHE PATH "Toolchain root")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Compiler
set(CMAKE_C_COMPILER   ${TOOLCHAIN_ROOT}/bin/aarch64-none-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_ROOT}/bin/aarch64-none-linux-gnu-g++)

# Sysroot (optional — toolchain's libc sysroot)
if(NOT DEFINED SYSROOT)
    set(SYSROOT "${TOOLCHAIN_ROOT}/aarch64-none-linux-gnu/libc")
    if(NOT EXISTS "${SYSROOT}/usr")
        # Fallback: some toolchains have sysroot at root level
        set(SYSROOT "${TOOLCHAIN_ROOT}/sysroot")
    endif()
endif()

if(EXISTS "${SYSROOT}")
    set(CMAKE_SYSROOT ${SYSROOT})
    message(STATUS "Using sysroot: ${SYSROOT}")
else()
    message(WARNING "Sysroot not found at ${SYSROOT} — set -DSYSROOT= manually")
endif()

# Search paths — only search the sysroot for libraries/headers, host for programs
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config: point to target's .pc files
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR}      "${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/share/pkgconfig")
