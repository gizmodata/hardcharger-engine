# shellcheck shell=bash
build_native_duckdb() {
  local extensions="$1" profile="$2" prefix="$CHARGEWIRE_DEPS_DIR/duckdb-install"
  local build="$CHARGEWIRE_DEPS_DIR/duckdb-build"
  cmake -S "$SRC" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DOVERRIDE_GIT_DESCRIBE="$DUCKDB_VERSION_NAME" -DDUCKDB_OPTIMIZATION_PROFILE="$profile" \
    '-DDUCKDB_CAPABILITIES=httplib;loadable_extensions' -DDUCKDB_EXTENSION_CONFIGS="$extensions" \
    -DBUILD_EXTENSIONS="" -DBUILD_SHELL=OFF -DBUILD_UNITTESTS=OFF -DENABLE_SANITIZER=OFF -DENABLE_UBSAN=OFF
  cmake --build "$build"
  cmake --install "$build"
  cp "$SRC/src/include/duckdb_v2.h" "$SRC/src/include/duckdb.hpp" "$prefix/include/"
  cp -R "$SRC/src/include/duckdb/." "$prefix/include/duckdb/"
}
