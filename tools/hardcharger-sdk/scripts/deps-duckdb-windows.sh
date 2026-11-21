#!/usr/bin/env bash
# Build the integration SDK from this engine checkout.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CHARGEWIRE_DEPS_DIR="${HARDCHARGER_DEPS_DIR:-$HERE/.deps}"
[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ] || { echo "set CMAKE_TOOLCHAIN_FILE (.github/cmake/windows-vcpkg.cmake)" >&2; exit 1; }

source "$HERE/scripts/native.sh"
SRC="$(git -C "$HERE" rev-parse --show-toplevel)"
DUCKDB_COMMIT="$(git -C "$SRC" rev-parse HEAD)"
DUCKDB_VERSION_NAME="${ENGINE_UPSTREAM_VERSION:?missing upstream version}"

# Windows keeps DuckDB's standard static-extension link behavior (DLL exports are C API).
WINDOWS_EXTENSIONS="$CHARGEWIRE_DEPS_DIR/duckdb-extensions-windows.cmake"
grep -v '^set(EXTENSION_STATIC_BUILD FALSE)$' "$HERE/scripts/duckdb-extensions.cmake" > "$WINDOWS_EXTENSIONS"
build_native_duckdb "$WINDOWS_EXTENSIONS" CLI
cmake -DDUCKDB_INSTALL_DIR="$CHARGEWIRE_DEPS_DIR/duckdb-install" -P "$HERE/scripts/prune-duckdb-install.cmake"

THIRD_PARTY="$CHARGEWIRE_DEPS_DIR/duckdb-install/include/duckdb_third_party"
rm -rf "$THIRD_PARTY" && mkdir -p "$THIRD_PARTY"
cp -R "$SRC/third_party/fmt/include/fmt" "$SRC/third_party/re2/re2" \
  "$SRC/third_party/fast_float/fast_float" "$THIRD_PARTY/"
