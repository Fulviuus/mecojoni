# SPDX-License-Identifier: MPL-2.0
#
# GDExtension adapter: mecojoni-core (no_std Rust) -> mecojoni-ffi (C ABI)
# -> this thin C++ layer -> Godot. Build from this directory:
#
#   cmake -B build
#   cmake --build build --target mecojoni.template_debug
#
# Shared libraries are written to ../demo/bin/ where the demo project's
# mecojoni.gdextension expects them. godot-cpp is expected at ./godot-cpp
# (git clone --branch godot-4.4-stable https://github.com/godotengine/godot-cpp).
cmake_minimum_required(VERSION 3.22)
project(mecojoni-godot LANGUAGES C CXX)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/CMakeLists.txt")
    message(FATAL_ERROR
        "godot-cpp is missing. Run:\n"
        "  git clone --depth 1 --branch godot-4.4-stable "
        "https://github.com/godotengine/godot-cpp "
        "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp")
endif()
add_subdirectory(godot-cpp SYSTEM)

# --- Rust C ABI library (mecojoni-ffi) --------------------------------------
# Always built in release: the grammar runtime's speed should not depend on
# the C++ configuration, and cargo makes the re-run a cheap no-op.
set(MECO_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
if(WIN32)
    set(MECO_FFI_LIB "${MECO_REPO_ROOT}/target/release/mecojoni_ffi.lib")
else()
    set(MECO_FFI_LIB "${MECO_REPO_ROOT}/target/release/libmecojoni_ffi.a")
endif()

find_program(CARGO_EXECUTABLE cargo REQUIRED)
add_custom_target(mecojoni_ffi_build
    COMMAND "${CARGO_EXECUTABLE}" build -p mecojoni-ffi --release
    WORKING_DIRECTORY "${MECO_REPO_ROOT}"
    COMMENT "cargo build -p mecojoni-ffi --release"
    VERBATIM
)

add_library(mecojoni_ffi STATIC IMPORTED)
set_target_properties(mecojoni_ffi PROPERTIES IMPORTED_LOCATION "${MECO_FFI_LIB}")
# Rust's std needs a few system libraries when linked statically.
if(WIN32)
    set_target_properties(mecojoni_ffi PROPERTIES INTERFACE_LINK_LIBRARIES
        "ws2_32;userenv;bcrypt;ntdll;advapi32")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(mecojoni_ffi PROPERTIES INTERFACE_LINK_LIBRARIES
        "pthread;dl;m")
endif()

# --- The extension shared libraries ------------------------------------------
set(MECO_SOURCES
    src/register_types.cpp
    src/meco_grammar.cpp
    src/meco_package_builder.cpp
)
set(MECO_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../demo/bin")

foreach(TARGET_ALIAS template_debug template_release)
    set(TARGET_NAME "mecojoni.${TARGET_ALIAS}")
    add_library(${TARGET_NAME} SHARED ${MECO_SOURCES})
    add_dependencies(${TARGET_NAME} mecojoni_ffi_build)
    target_include_directories(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../mecojoni-ffi/include")
    target_link_libraries(${TARGET_NAME} PRIVATE
        godot-cpp::${TARGET_ALIAS}
        mecojoni_ffi)

    # Name libraries the way the .gdextension file addresses them:
    # libmecojoni.<platform>.<target>.<ext>, e.g. libmecojoni.macos.template_debug.dylib
    get_target_property(GODOT_PLATFORM godot-cpp.${TARGET_ALIAS} GODOTCPP_PLATFORM)
    set_target_properties(${TARGET_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        PREFIX "lib"
        OUTPUT_NAME "mecojoni.${GODOT_PLATFORM}.${TARGET_ALIAS}"
        LIBRARY_OUTPUT_DIRECTORY "${MECO_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${MECO_OUTPUT_DIR}"
    )
endforeach()
