cmake_minimum_required(VERSION 3.16)
project(ws_mod_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(OpenSSL REQUIRED)
find_package(Catch2 3 REQUIRED)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------
add_executable(ws_tests
    test_ws_proto.cpp
    test_ws_gmcp.cpp
    test_ws_tls.cpp
    ../ws_proto.cpp
    ../ws_gmcp.cpp
    ../ws_config.cpp
)

target_include_directories(ws_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..  # src/
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(ws_tests PRIVATE
    Catch2::Catch2WithMain
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# ---------------------------------------------------------------------------
# Compile options
# ---------------------------------------------------------------------------
target_compile_options(ws_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Debug>:
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    >
)

target_link_options(ws_tests PRIVATE
    $<$<CONFIG:Debug>:
        -fsanitize=address,undefined
    >
)

# ---------------------------------------------------------------------------
# Test discovery
# ---------------------------------------------------------------------------
include(CTest)
include(Catch)
catch_discover_tests(ws_tests
    REPORTER console
    OUTPUT_ON_FAILURE
)
