cmake_minimum_required(VERSION 3.16)
project(rk_firmware_flashd VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Debug build by default (pass -DCMAKE_BUILD_TYPE=Release to override)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Add debug symbols in all build types
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# --- Dependencies via pkg-config (system-wide linking) ---
find_package(PkgConfig REQUIRED)

pkg_check_modules(MHD REQUIRED libmicrohttpd)
pkg_check_modules(LIBSERIALPORT REQUIRED libserialport)

# --- Collect source files ---
file(GLOB SOURCES "src/*.cpp")
list(APPEND SOURCES "main.cpp")

# --- Build target ---
add_executable(${PROJECT_NAME} ${SOURCES})

# --- Include paths: own headers + submodule headers ---
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${MHD_INCLUDE_DIRS}
    ${LIBSERIALPORT_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/libmicrohttpd/src/include
    ${CMAKE_SOURCE_DIR}/libserialport/include
    ${CMAKE_SOURCE_DIR}/json/include
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${MHD_LIBRARY_DIRS}
    ${LIBSERIALPORT_LIBRARY_DIRS}
)

# --- Link against system-wide packages ---
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${MHD_LIBRARIES}
    ${LIBSERIALPORT_LIBRARIES}
    pthread
)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${MHD_CFLAGS_OTHER}
    ${LIBSERIALPORT_CFLAGS_OTHER}
    -Wall -Wextra -Wpedantic
)

# --- Install ---
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# --- Tests ---
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
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
    target_compile_options(test_logger PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME logger_tests COMMAND test_logger)

    add_executable(test_checksum
        test/test_main.cpp
        test/test_checksum.cpp
    )
    target_include_directories(test_checksum PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/test
    )
    target_compile_options(test_checksum PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME checksum_tests COMMAND test_checksum)
endif()
