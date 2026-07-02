cmake_minimum_required(VERSION 3.16)
project(rk_firmware_flashd VERSION 0.1.0 LANGUAGES C CXX)

# =============================================================================
# Build options
# =============================================================================
option(BUILD_TESTS     "Build unit tests"               ON)
option(ENABLE_RCE      "Enable AT+EXEC remote command"  OFF)
option(ENABLE_REBOOT   "Enable AT+REBOOT"               ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# =============================================================================
# Compiler flags
# =============================================================================
add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_options("$<$<CONFIG:Debug>:-g>")
add_compile_options("$<$<CONFIG:Release>:-O2>")

# =============================================================================
# Dependencies
# =============================================================================
if(CMAKE_CROSSCOMPILING)
    # Cross-compilation: use sysroot pkg-config or manual paths
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(MHD           REQUIRED libmicrohttpd)
        pkg_check_modules(LIBSERIALPORT REQUIRED libserialport)
    else()
        message(STATUS "pkg-config not found — using manual library paths")
        # User must provide: -DMHD_LIB_DIR=... -DLIBSERIALPORT_LIB_DIR=...
        if(NOT DEFINED MHD_LIB_DIR)
            message(FATAL_ERROR "Cross-compilation: set -DMHD_LIB_DIR=/path/to/lib")
        endif()
        if(NOT DEFINED LIBSERIALPORT_LIB_DIR)
            message(FATAL_ERROR "Cross-compilation: set -DLIBSERIALPORT_LIB_DIR=/path/to/lib")
        endif()
        set(MHD_LIBRARIES           microhttpd)
        set(MHD_INCLUDE_DIRS       "")
        set(MHD_LIBRARY_DIRS       ${MHD_LIB_DIR})
        set(MHD_CFLAGS_OTHER       "")
        set(LIBSERIALPORT_LIBRARIES serialport)
        set(LIBSERIALPORT_INCLUDE_DIRS "")
        set(LIBSERIALPORT_LIBRARY_DIRS ${LIBSERIALPORT_LIB_DIR})
        set(LIBSERIALPORT_CFLAGS_OTHER "")
    endif()

    # Tests can't run on host when cross-compiling
    set(BUILD_TESTS OFF CACHE BOOL "Tests disabled for cross-compilation" FORCE)
    message(STATUS "Cross-compiling — tests disabled")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MHD           REQUIRED libmicrohttpd)
    pkg_check_modules(LIBSERIALPORT REQUIRED libserialport)
endif()

# =============================================================================
# Sources
# =============================================================================
file(GLOB MAIN_SOURCES    "src/*.cpp")
file(GLOB HANDLER_SOURCES "src/at_handler/*.cpp")
set(ALL_SOURCES ${MAIN_SOURCES} ${HANDLER_SOURCES} main.cpp
    ${CMAKE_SOURCE_DIR}/3rd_party/tiny-AES-c/aes.c)

# =============================================================================
# Main executable
# =============================================================================
add_executable(${PROJECT_NAME} ${ALL_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/3rd_party/json/include
    ${CMAKE_SOURCE_DIR}/3rd_party/libmicrohttpd/src/include
    ${CMAKE_SOURCE_DIR}/3rd_party/libserialport/include
    ${CMAKE_SOURCE_DIR}/3rd_party/tiny-AES-c
    ${MHD_INCLUDE_DIRS}
    ${LIBSERIALPORT_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${MHD_LIBRARIES}
    ${LIBSERIALPORT_LIBRARIES}
    pthread
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${MHD_LIBRARY_DIRS}
    ${LIBSERIALPORT_LIBRARY_DIRS}
)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${MHD_CFLAGS_OTHER}
    ${LIBSERIALPORT_CFLAGS_OTHER}
)

# =============================================================================
# Feature flags
# =============================================================================
if(ENABLE_RCE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ALLOW_RCE)
    message(STATUS "RCE enabled: AT+EXEC is ACTIVE")
else()
    message(STATUS "RCE disabled: AT+EXEC returns error (use -DENABLE_RCE=ON to enable)")
endif()

if(ENABLE_REBOOT)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ALLOW_REBOOT)
    message(STATUS "Reboot enabled: AT+REBOOT is ACTIVE")
else()
    message(STATUS "Reboot disabled: AT+REBOOT returns error (use -DENABLE_REBOOT=ON to enable)")
endif()

# =============================================================================
# Install
# =============================================================================
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# =============================================================================
# Tests
# =============================================================================
if(BUILD_TESTS)
    enable_testing()

    # --- test_logger ---
    add_executable(test_logger
        test/test_main.cpp
        test/test_logger.cpp
        src/logger.cpp
    )
    target_include_directories(test_logger PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/test
    )
    target_link_libraries(test_logger PRIVATE pthread)
    add_test(NAME logger_tests COMMAND test_logger)

    # --- test_checksum ---
    add_executable(test_checksum
        test/test_main.cpp
        test/test_checksum.cpp
    )
    target_include_directories(test_checksum PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/test
    )
    add_test(NAME checksum_tests COMMAND test_checksum)

    # --- test_aes ---
    add_executable(test_aes
        test/test_main.cpp
        test/test_aes.cpp
        ${CMAKE_SOURCE_DIR}/3rd_party/tiny-AES-c/aes.c
    )
    target_include_directories(test_aes PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/test
        ${CMAKE_SOURCE_DIR}/3rd_party/tiny-AES-c
    )
    add_test(NAME aes_tests COMMAND test_aes)

    # --- test_at_command ---
    file(GLOB AT_HANDLER_SOURCES "src/at_handler/*.cpp")
    add_executable(test_at_command
        test/test_main.cpp
        test/test_at_command.cpp
        src/at_command.cpp
        ${AT_HANDLER_SOURCES}
        src/serial_daemon.cpp
        src/serial_port.cpp
        src/logger.cpp
        src/config.cpp
        ${CMAKE_SOURCE_DIR}/3rd_party/tiny-AES-c/aes.c
    )
    target_include_directories(test_at_command PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/test
        ${CMAKE_SOURCE_DIR}/3rd_party/json/include
        ${CMAKE_SOURCE_DIR}/3rd_party/tiny-AES-c
        ${LIBSERIALPORT_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/3rd_party/libserialport/include
    )
    target_link_libraries(test_at_command PRIVATE
        ${LIBSERIALPORT_LIBRARIES}
        pthread
    )
    target_compile_options(test_at_command PRIVATE
        ${LIBSERIALPORT_CFLAGS_OTHER}
    )
    add_test(NAME at_command_tests COMMAND test_at_command)
endif()
