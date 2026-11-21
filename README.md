<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/gizmodata/hardcharger/main/docs/assets/hardcharger-banner-dark.png">
    <img alt="Hardcharger" width="530" src="https://raw.githubusercontent.com/gizmodata/hardcharger/main/docs/assets/hardcharger-banner-light.png">
  </picture>
</p>

# Hardcharger Engine

Hardcharger Engine is GizmoData’s performance-tuned fork of DuckDB for analytical workloads. The first release builds on DuckDB 2.0.1 and improves selective joins, filtered scans, and repeated string matching. You can run it directly with the DuckDB CLI or embed it through the DuckDB C API. [Hardcharger server](https://github.com/gizmodata/hardcharger) ships separately.

[![Engine CI](https://github.com/gizmodata/hardcharger-engine/actions/workflows/engine.yml/badge.svg)](https://github.com/gizmodata/hardcharger-engine/actions/workflows/engine.yml)
[![Engine release](https://img.shields.io/github/v/release/gizmodata/hardcharger-engine?label=engine)](https://github.com/gizmodata/hardcharger-engine/releases)
[![MIT license](https://img.shields.io/badge/license-MIT-green)](LICENSE)

## Why we forked

DB Olympics Q318 exposed a join where the engine spent most of its time checking candidate pairs it could rule out with a more selective hash key. Fixing that plan required a change inside DuckDB’s optimizer. Maintaining a fork lets us ship and test that change on Hardcharger’s release schedule, alongside a small set of scan and string optimizations. The [differences from DuckDB 2.0.1](docs/optimizations.md) describe the rules, their limits, and what we measured on other queries. The same document covers the [seven engine changes used by the Hardcharger server](docs/optimizations.md#engine-changes-used-by-the-hardcharger-server).

## Run the engine

Choose the CLI archive for your operating system and CPU from the [engine releases](https://github.com/gizmodata/hardcharger-engine/releases). Download its accompanying `SHA256SUMS` file to verify the archive. Extract it and run this query from the extracted directory.

```sh
./duckdb -c "SELECT version(), sum(i) FROM range(100) AS r(i);"
```

For embedded applications, the shared-library archive includes `libduckdb` and the DuckDB C headers. Existing code can continue to include `duckdb.h` and link with `-lduckdb`. [DuckDB C API](https://duckdb.org/docs/stable/clients/c/overview) · [DuckDB SQL reference](https://duckdb.org/docs/stable/sql/introduction)

## Build and test

From a checkout with CMake, Python 3, and a C++17 compiler, run the following from the repository root.

```sh
make release
make allunit
```

Engine release CI runs DuckDB’s full registered test suite, including slow cases and the optimizer and execution regressions added by GizmoData. The release workflow also tests the CLI and SQL logic test tools before it publishes binaries. [Test suite](test) · [Release workflow](.github/workflows/engine.yml)

## Acknowledgments

We thank the DuckDB developers and contributors for the engine, SQL implementation, and test suite that Hardcharger Engine builds on. GizmoData independently maintains and publishes this fork. DuckDB is a trademark of the DuckDB Foundation. [DuckDB project](https://github.com/duckdb/duckdb) · [Trademark guidelines](https://duckdb.org/trademark_guidelines#attribution)

The upstream code is copyright 2018–2026 Stichting DuckDB Foundation and is distributed under the [MIT license](LICENSE).
