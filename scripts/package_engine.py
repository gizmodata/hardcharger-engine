"""Package the CLI and C SDK and exercise both from the unpacked archives."""

import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(*args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs).strip()


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def package(build, dest):
    system = {"Darwin": "macos", "Linux": "linux"}[platform.system()]
    arch = {"x86_64": "amd64", "aarch64": "arm64", "arm64": "arm64"}[platform.machine()]
    lib = "libduckdb.dylib" if system == "macos" else "libduckdb.so"
    dest.mkdir(parents=True, exist_ok=True)
    licenses = [
        p for p in (ROOT / "third_party").rglob("*")
        if p.is_file() and p.name.upper().startswith(("LICENSE", "COPYING", "NOTICE"))
    ]
    with tempfile.TemporaryDirectory() as temp:
        tmp = Path(temp)
        for kind, files in {
            "cli": [build / "duckdb"],
            "shared-libs": [build / "src" / lib, *sorted((ROOT / "src/include").glob("duckdb*.h"))],
        }.items():
            stage = tmp / kind
            stage.mkdir()
            for p in files:
                shutil.copy2(p, stage / p.name)
            manifest = {
                "commit": run("git", "-C", str(ROOT), "rev-parse", "HEAD"),
                "platform": f"{system}-{arch}",
                "version_output": run(build / "duckdb", "-version"),
                "sha256": {p.name: digest(p) for p in files},
            }
            (stage / "BUILD.json").write_text(json.dumps(manifest, indent=2) + "\n")
            archive = dest / f"hardcharger-engine-{kind}-{system}-{arch}.tar.gz"
            with tarfile.open(archive, "w:gz") as tar:
                for p in sorted(stage.iterdir()):
                    tar.add(p, arcname=p.name)
                tar.add(ROOT / "LICENSE", arcname="LICENSE")
                for p in sorted(licenses):
                    tar.add(p, arcname=str(Path("notices") / p.relative_to(ROOT)))
            with tarfile.open(archive) as tar:
                tar.extractall(tmp / "unpacked" / kind, filter="data")
        cli = tmp / "unpacked/cli/duckdb"
        if run(cli, "-csv", "-noheader", "-c", "SELECT sum(i) FROM range(100) r(i)") != "4950":
            raise RuntimeError("CLI query failed")
        sdk = tmp / "unpacked/shared-libs"
        source = tmp / "consumer.c"
        source.write_text(
            '#include "duckdb.h"\n'
            'int main(void) { duckdb_database db; duckdb_connection c; duckdb_result r;\n'
            ' if (duckdb_open(NULL, &db) || duckdb_connect(db, &c)) return 1;\n'
            ' if (duckdb_query(c, "SELECT 42", &r)) return 2;\n'
            ' int ok = duckdb_value_int32(&r, 0, 0) == 42; duckdb_destroy_result(&r);\n'
            ' duckdb_disconnect(&c); duckdb_close(&db); return !ok; }\n'
        )
        consumer = tmp / "consumer"
        run(os.environ.get("CC", "cc"), source, f"-I{sdk}", f"-L{sdk}", "-lduckdb", f"-Wl,-rpath,{sdk}", "-o", consumer)
        run(consumer)


if __name__ == "__main__":
    package(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
