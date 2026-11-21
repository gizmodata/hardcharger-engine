#!/usr/bin/env bash
# Build the exact SDK layout consumed by Hardcharger, including internal headers,
# static libraries, extension registration sources and platform-specific dependencies.
set -euo pipefail
flavor=${1:?SDK platform required}
sdk=$(cd "$(dirname "$0")" && pwd)
root=$(git -C "$sdk" rev-parse --show-toplevel)
export HARDCHARGER_DEPS_DIR=${HARDCHARGER_DEPS_DIR:-"$root/build/hardcharger-sdk/$flavor"}
mkdir -p "$HARDCHARGER_DEPS_DIR"
export CHARGEWIRE_DEPS_DIR="$HARDCHARGER_DEPS_DIR"
export ENGINE_UPSTREAM_VERSION=v2.0.1
export CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-3}
if [ "$flavor" = linux-amd64-baseline ]; then export HARDCHARGER_CPU_TARGET=baseline; fi
case "$flavor" in
  android-*)
    export HARDCHARGER_ANDROID_ABI="${flavor#android-}"
    export ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_HOME:?}/ndk/28.2.13676358}"
    export CMAKE_TOOLCHAIN_FILE="$sdk/cmake/android.toolchain.cmake" ;;
  ios-*)
    export HARDCHARGER_IOS_SDK="${flavor#ios-}"
    export CMAKE_TOOLCHAIN_FILE="$sdk/cmake/ios.toolchain.cmake" ;;
  windows-*)
    export CMAKE_TOOLCHAIN_FILE="$(cygpath -m "$sdk/cmake/windows-vcpkg.cmake")" ;;
  linux-arm64-tsan)
    export CC=clang CXX=clang++
    export CFLAGS='-fsanitize=thread -gline-tables-only'
    export CXXFLAGS="$CFLAGS" LDFLAGS=-fsanitize=thread ;;
  linux-amd64|linux-amd64-baseline|linux-arm64|linux-arm64-ubuntu|macos-arm64) ;;
  *) echo "Unknown SDK platform $flavor" >&2; exit 2 ;;
esac
case "$flavor" in
  windows-*) "$sdk/scripts/deps-duckdb-windows.sh" ;;
  linux-arm64-tsan) "$sdk/scripts/build-tsan-duckdb.sh" duckdb ;;
  linux-arm64-ubuntu) "$sdk/scripts/deps-duckdb.sh" ;;
  *)
    export OPENSSL_ROOT_DIR="$HARDCHARGER_DEPS_DIR/openssl-install"
    "$sdk/scripts/deps-openssl.sh"
    "$sdk/scripts/deps-duckdb.sh" ;;
esac
"$sdk/package.sh" "$flavor" "$HARDCHARGER_DEPS_DIR/duckdb-install" "$root/dist"
