# Differences from DuckDB 2.0.1

Hardcharger Engine v2.0.1 starts from upstream commit `8d78114e90a53823803660bf821f6073ac87da8c`. The initial performance changes end at `aef3086626218f878d821f7936e3c20cc4940b12`. These 14 performance commits touch 19 files. The engine also includes the seven integrations below. The public C headers, SQL syntax, and database file format are unchanged by this patch set. [Upstream source](https://github.com/duckdb/duckdb/tree/8d78114e90a53823803660bf821f6073ac87da8c) · [Performance diff](https://github.com/gizmodata/hardcharger-engine/compare/8d78114e90a53823803660bf821f6073ac87da8c...aef3086626218f878d821f7936e3c20cc4940b12)

## What changed

| Area | Change and applicability | Source and regression coverage |
|---|---|---|
| Disjunctive joins under DISTINCT | When two tables share a weak join key such as tenant, checking an additional user-ID-or-email condition can produce many candidate pairs. For eligible inner joins, the optimizer builds two selective equijoins, keeps the common key in both, and lets the existing DISTINCT remove overlap. It checks input repeatability, key selectivity, and whether changing the input order could change an observable DISTINCT representative. | [Optimizer](../src/optimizer/disjunctive_join.cpp) · [Join tests](../test/optimizer/disjunctive_join.test) |
| Timestamp-to-date joins | Casting timestamps to dates collapses many timestamps onto the same calendar day. The join estimator uses a bounded day span for eligible cast keys. Raw timestamp equality retains its existing estimate. This can change join order and intermediate result sizes. | [Estimator](../src/optimizer/join_order/cardinality_estimator.cpp) · [Date-domain tests](../test/optimizer/statistics/timestamp_date_join_domain.test) |
| Integer range filters | A scan can apply compatible lower and upper bounds together in one pass over integer values. | [Filter executor](../src/planner/table_filter_state.cpp) · [Range tests](../test/sql/filter/fused_integral_scan_range.test) |
| Filtered column scans | A filtered vector scan reuses its check for updates to the data and validity columns when deciding whether compressed selection is available. The generic path still applies visible updates. An experimental direct-fetch path for sparse row IDs was reverted after it slowed warm queries. | [Scan implementation](../src/storage/table/standard_column_data.cpp) · [Transaction visibility tests](../test/sql/storage/test_sparse_rowid_payload_fetch.test) |
| Automatic read-ahead | The first decoder can start after its successor is scheduled, before the entire speculative queue fills. Explicitly configured read-ahead depth keeps its existing behavior. | [Scheduler](../src/parallel/scan_read_ahead.cpp) · [Read-ahead tests](../test/api/test_read_ahead_progress.cpp) |
| Substring search | Short searches reuse the first candidate position. Medium-length comparisons use two bounded word loads, avoiding redundant comparisons while respecting input boundaries. | [Substring search](../src/function/scalar/string/contains.cpp) · [Boundary tests](../test/sql/function/string/test_contains_candidate_bounds.test) |
| Regular expression extraction | The executor samples for adjacent equal inputs before caching a prior capture. Eligible patterns can stop after the requested captures when the remaining suffix is an unrestricted wildcard. | [Regex execution](../src/function/scalar/string/regexp.cpp) · [Adjacent inputs](../test/sql/function/string/regex_extract_adjacent.test) · [Suffix tests](../test/sql/function/string/regex_extract_unrestricted_suffix.test) |

## Engine changes used by the Hardcharger server

These seven changes were previously applied by Hardcharger’s CMake scripts during each source build. They now live in the engine source. The server still makes authorization decisions. Engine hooks give it the binding context those decisions require. [Integration commit](https://github.com/gizmodata/hardcharger-engine/commit/a90a7094e51)

| Change | Effect | Implementation |
|---|---|---|
| Query progress sampling | Hardcharger waits for the configured progress delay and then samples at most once per 100 ms. Standalone clients retain DuckDB’s per-task progress updates. This reduces the time workers spend computing estimates on queries with many pipelines. | [Progress bar](../src/common/progress_bar/progress_bar.cpp) |
| Arrow export of 128-bit integers | When exporting HUGEINT or UHUGEINT as Arrow decimal128(38,0), a value outside the 38-digit range raises a conversion error. Values in range keep their exact value. DuckDB’s lossless Arrow extension types retain the full integer range. | [Arrow appender](../src/common/arrow/arrow_appender.cpp) |
| Transaction cleanup after binding | When a transaction recorded as read-only reaches commit with changes made during binding, the engine reports a transaction error and runs rollback and cleanup. Other connections can continue using the database. | [Transaction manager](../src/transaction/duck_transaction_manager.cpp) |
| View binding and owner privileges | An optional binder hook identifies the part of a query plan produced by a view. Hardcharger uses it to check the view’s reads under its owner’s privileges. | [View binding](../src/planner/binder/tableref/bind_basetableref.cpp) |
| Name resolution in stored expressions | The binder carries the defining object’s scope into function and type resolution for stored expressions. Hardcharger uses this context for views, defaults, constraints, generated columns, triggers and policies. | [Catalog lookup](../src/catalog/catalog.cpp) · [Binder](../src/planner/binder.cpp) |
| Read-only execution | Persistent sequence changes are registered as writes. An optional hook lets Hardcharger apply its read-only policy when binding a table function. | [Sequences](../src/catalog/catalog_entry/sequence_catalog_entry.cpp) · [Table function binding](../src/planner/binder/tableref/bind_table_function.cpp) |
| Extension repository opt-in | Hardcharger requires an administrator’s explicit decision before trusting a new repository, including a repository named by its host. Existing trusted repositories remain usable, and DuckDB continues to verify extension signatures. | [Repository policy](../src/main/extension/extension_repository_manager.cpp) |

## What we measured through Hardcharger

We swapped only the engine source in a Hardcharger v2.14.0 comparison build on one Apple Silicon Mac. Both builds used two engine threads and a 6 GiB memory limit. The DB Olympics runs covered all 409 queries against the final 2025 Q4 snapshot, with one warm-up, six measured passes, alternating execution order, and CSV result retrieval included. The benchmark checker accepted all 11,452 results across SF1 and SF10. These are local timings.

| SF10 measurement | Official DuckDB source | Engine fork | Interpretation |
|---|---:|---:|---|
| DB Olympics Q318, 12 extra warm pairs | 3,669.76 ms | 19.15 ms | 191.63× paired speedup. All 24 results passed the checker. |
| DB Olympics, sum of 409 query medians | 9.550 s | 5.640 s | 40.94% less time. Q318 accounted for 91.8% of the saving. |
| DB Olympics, remaining 408 queries | | | 5.39% less summed time. Individual queries varied. Q311 went from 5.84 to 5.92 ms. |
| TPC-H, warm suite | 4.365 s | 4.275 s | 2.22% lower paired time. The 95% interval was 1.26% to 2.81% lower. |
| TPC-DS, warm suite | 15.682 s | 15.538 s | 0.77% lower paired time. The interval ranged from 1.78% lower to 0.61% higher. |

The DB Olympics scoring formula adds 10 ms to each query median and weights queries geometrically. On that measure the fork improved 2.84% at SF10 and 0.53% at SF1. The local run measured queries only. TPC timings included fetching the complete Arrow result over loopback, with six paired starts per build. All 4,356 checked TPC results matched the upstream answer files.
