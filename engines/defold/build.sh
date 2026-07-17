#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# Stages the Mecojoni C ABI into the Defold native extension:
#   1. builds the mecojoni-ffi static library (release),
#   2. refreshes the extension's copy of the C ABI header,
#   3. copies the static library into lib/<platform>/.
#
# The extension links this prebuilt static lib on Defold's build server, which
# compiles the C++ bridge (src/mecojoni_ext.cpp) against include/mecojoni.h.
#
# Usage: engines/defold/build.sh [defold-platform]
#   defold-platform defaults to the host (arm64-macos / x86_64-macos).
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
ext_dir="${script_dir}/demo/mecojoni"
ffi_header="${repo_root}/engines/godot/mecojoni-ffi/include/mecojoni.h"

# Map the host to a Defold platform id when one is not given explicitly.
platform="${1:-}"
if [[ -z "${platform}" ]]; then
  # Defold's extension lib folders use the legacy -osx suffix on macOS.
  case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) platform="arm64-osx" ;;
    Darwin-x86_64) platform="x86_64-osx" ;;
    Linux-x86_64) platform="x86_64-linux" ;;
    Linux-aarch64) platform="arm64-linux" ;;
    *) echo "Unknown host; pass a Defold platform id explicitly." >&2; exit 1 ;;
  esac
fi

echo "Building mecojoni-ffi (release) for ${platform}..."
cargo build -p mecojoni-ffi --release --manifest-path "${repo_root}/Cargo.toml"

echo "Refreshing ${ext_dir}/include/mecojoni.h"
cp "${ffi_header}" "${ext_dir}/include/mecojoni.h"

lib_dir="${ext_dir}/lib/${platform}"
mkdir -p "${lib_dir}"
echo "Copying static library into ${lib_dir}"
cp "${repo_root}/target/release/libmecojoni_ffi.a" "${lib_dir}/libmecojoni_ffi.a"

echo "Done. Build the demo with bob or open engines/defold/demo in the editor."
