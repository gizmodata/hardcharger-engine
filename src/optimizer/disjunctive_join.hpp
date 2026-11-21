//===----------------------------------------------------------------------===//
//                         DuckDB
//
// disjunctive_join.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/planner/logical_operator.hpp"

namespace duckdb {
class Optimizer;

//! Split a disjunctive equijoin below a duplicate-eliminating projection into two selective hash joins.
void RewriteDisjunctiveJoins(Optimizer &optimizer, unique_ptr<LogicalOperator> &op);

} // namespace duckdb
