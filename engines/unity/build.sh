#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# Stages the Mecojoni C ABI into the Unity demo project:
#   1. builds the mecojoni-ffi dynamic library (release),
#   2. copies it into Assets/Mecojoni/Plugins/<platform>/.
#
# Unity's C# wrapper (Assets/Mecojoni/Runtime) P/Invokes this library
# directly; there is no intermediate C++ layer.
#
# Usage: engines/unity/build.sh
# Builds for the host platform. For other targets, build mecojoni-ffi with
# the matching Rust target and drop the library into the corresponding
# Plugins folder (see the table in engines/unity/README.md).
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
plugins_dir="${script_dir}/demo/Assets/Mecojoni/Plugins"

echo "Building mecojoni-ffi (release)..."
cargo build -p mecojoni-ffi --release --manifest-path "${repo_root}/Cargo.toml"

case "$(uname -s)" in
  Darwin)
    dest="${plugins_dir}/macOS"
    mkdir -p "${dest}"
    cp "${repo_root}/target/release/libmecojoni_ffi.dylib" "${dest}/libmecojoni_ffi.dylib"
    echo "Staged ${dest}/libmecojoni_ffi.dylib ($(lipo -archs "${dest}/libmecojoni_ffi.dylib" 2>/dev/null || echo unknown))"
    ;;
  Linux)
    dest="${plugins_dir}/Linux/$(uname -m)"
    mkdir -p "${dest}"
    cp "${repo_root}/target/release/libmecojoni_ffi.so" "${dest}/libmecojoni_ffi.so"
    echo "Staged ${dest}/libmecojoni_ffi.so"
    ;;
  *)
    echo "Unsupported host $(uname -s); build mecojoni-ffi manually and copy" >&2
    echo "the library into ${plugins_dir}/<platform>/." >&2
    exit 1
    ;;
esac

echo "Done. Open engines/unity/demo in Unity and press Play."
