#!/usr/bin/env bash
set -euo pipefail
flavor=${1:?SDK platform required}
prefix=${2:?installed SDK required}
dest=${3:?output directory required}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
test -s "$prefix/include/duckdb_v2.h"
test -s "$prefix/include/duckdb.hpp"
test -d "$prefix/include/duckdb"
test -d "$prefix/include/duckdb_third_party/fmt"
case "$flavor" in
  windows-*) test -s "$prefix/lib/duckdb_static.lib" ;;
  *)
    test -s "$prefix/lib/libduckdb_static.a"
    test -s "$prefix/share/duckdb-static/duckdb_static_extension_loader.c" ;;
esac
mkdir -p "$prefix/share/hardcharger-engine" "$dest"
git -C "$root" rev-parse HEAD > "$prefix/share/hardcharger-engine/commit"
printf '%s\n' "$flavor" > "$prefix/share/hardcharger-engine/platform"
cp "$root/LICENSE" "$prefix/share/hardcharger-engine/LICENSE"
mkdir -p "$prefix/share/hardcharger-engine/extension-notices"
cp -R "$root/tools/hardcharger-sdk/notices/." "$prefix/share/hardcharger-engine/extension-notices/"
# Include the vendored notices and build-recipe license in each binary SDK.
cp "$root/tools/hardcharger-sdk/LICENSE" "$prefix/share/hardcharger-engine/LICENSE-recipes"
cp "$root/tools/hardcharger-sdk/NOTICE" "$prefix/share/hardcharger-engine/NOTICE-recipes"
(cd "$root" && git ls-files -z -- 'third_party/**' 'extension/**' |
  python3 -c 'import os, pathlib, shutil, sys
dest = pathlib.Path(sys.argv[1])
for raw in sys.stdin.buffer.read().split(b"\0"):
    src = pathlib.Path(os.fsdecode(raw))
    if src.name.upper().startswith(("COPYING","LICENSE","NOTICE")) and src.is_file():
        target = dest / src
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, target)' "$prefix/share/hardcharger-engine/notices")
# GNU tar treats a Windows drive letter in the archive name as a remote host.
tar -czf - -C "$prefix" . > "$dest/hardcharger-engine-sdk-$flavor.tar.gz"
