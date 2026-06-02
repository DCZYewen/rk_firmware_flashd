cmake_minimum_required(VERSION 3.16)
project(rk_firmware_flashd VERSION 0.1.0 LANGUAGES C CXX)

# =============================================================================
# Build options
# =============================================================================
option(BUILD_TESTS     "Build unit tests"               ON)
option(ENABLE_RCE      "Enable AT+EXEC remote command"  OFF)

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
find_package(PkgConfig REQUIRED)
pkg_check_modules(MHD           REQUIRED libmicrohttpd)
pkg_check_modules(LIBSERIALPORT REQUIRED libserialport)

# =============================================================================
# Sources
# =============================================================================
file(GLOB MAIN_SOURCES    "src/*.cpp")
file(GLOB HANDLER_SOURCES "src/at_handler/*.cpp")
set(ALL_SOURCES ${MAIN_SOURCES} ${HANDLER_SOURCES} main.cpp)

# =============================================================================
# Main executable
# =============================================================================
add_executable(${PROJECT_NAME} ${ALL_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/3rd_party/json/include
    ${CMAKE_SOURCE_DIR}/3rd_party/libmicrohttpd/src/include
    ${CMAKE_SOURCE_DIR}/3rd_party/libserialport/include
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
endif()
