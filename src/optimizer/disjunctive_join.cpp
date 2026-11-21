#include "duckdb/optimizer/disjunctive_join.hpp"

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/optimizer/optimizer.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_conjunction_expression.hpp"
#include "duckdb/planner/filter/expression_filter.hpp"
#include "duckdb/planner/logical_operator_deep_copy.hpp"
#include "duckdb/planner/operator/logical_comparison_join.hpp"
#include "duckdb/planner/operator/logical_cteref.hpp"
#include "duckdb/planner/operator/logical_distinct.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/planner/operator/logical_projection.hpp"
#include "duckdb/planner/operator/logical_set_operation.hpp"

namespace duckdb {
namespace {

bool RepeatableJoinInput(LogicalOperator &op) {
	if (op.type == LogicalOperatorType::LOGICAL_CTE_REF) {
		auto &ref = op.Cast<LogicalCTERef>();
		return !ref.is_recurring && ref.correlated_columns == 0;
	}
	if (op.type != LogicalOperatorType::LOGICAL_GET) {
		return false;
	}
	auto &scan = op.Cast<LogicalGet>();
	if (!scan.GetTable() || !scan.GetTable()->TryGetDuckTableEntry() || scan.function.GetName() != "seq_scan" ||
	    !scan.bind_data || !scan.function.statistics_extended || !scan.children.empty() ||
	    scan.extra_info.sample_options || scan.dynamic_filters || scan.at_clause || scan.bind_info ||
	    !scan.scan_partition_indices.empty() || scan.source_ordinality != OrdinalityType::WITHOUT_ORDINALITY) {
		return false;
	}
	for (const auto &entry : scan.table_filters) {
		const auto &filter = entry.Filter();
		if (filter.filter_type != TableFilterType::EXPRESSION_FILTER) {
			return false;
		}
		const auto &expr = *filter.Cast<ExpressionFilter>().expr;
		if (expr.IsVolatile() || expr.CanThrow() || expr.HasParameter()) {
			return false;
		}
	}
	return true;
}

bool IsColumn(const Expression &expr) {
	return expr.GetExpressionClass() == ExpressionClass::BOUND_COLUMN_REF &&
	       expr.Cast<BoundColumnRefExpression>().Depth() == 0;
}

bool GetEqualityKeys(const Expression &expr, const vector<ColumnBinding> &left, const vector<ColumnBinding> &right,
                     JoinCondition &result) {
	if (expr.GetExpressionType() != ExpressionType::COMPARE_EQUAL) {
		return false;
	}
	auto &cmp = expr.Cast<BoundFunctionExpression>();
	auto &lhs = BoundComparisonExpression::Left(cmp);
	auto &rhs = BoundComparisonExpression::Right(cmp);
	if (!IsColumn(lhs) || !IsColumn(rhs)) {
		return false;
	}
	auto l = lhs.Cast<BoundColumnRefExpression>().Binding();
	auto r = rhs.Cast<BoundColumnRefExpression>().Binding();
	if (std::find(left.begin(), left.end(), l) != left.end() &&
	    std::find(right.begin(), right.end(), r) != right.end()) {
		result = JoinCondition(lhs.Copy(), rhs.Copy(), ExpressionType::COMPARE_EQUAL);
		return true;
	}
	if (std::find(left.begin(), left.end(), r) != left.end() &&
	    std::find(right.begin(), right.end(), l) != right.end()) {
		result = JoinCondition(rhs.Copy(), lhs.Copy(), ExpressionType::COMPARE_EQUAL);
		return true;
	}
	return false;
}

idx_t JoinKeyDistinctCount(ClientContext &context, LogicalGet &scan, const Expression &key) {
	auto binding = key.Cast<BoundColumnRefExpression>().Binding();
	if (binding.table_index != scan.table_index || binding.column_index.GetIndex() >= scan.GetColumnIds().size()) {
		return 0;
	}
	const auto &column = scan.GetColumnIndex(binding);
	if (!column.HasPrimaryIndex() || column.IsVirtualColumn() || column.HasChildren()) {
		return 0;
	}
	TableFunctionGetStatisticsInput input(scan.bind_data.get(), column);
	auto statistics = scan.function.statistics_extended(context, input);
	return statistics ? statistics->GetDistinctCount() : 0;
}

// A second scan and hash build only pay when the original hash key admits many candidates
// and both equality alternatives have substantially more distinct values on a native scan.
bool ShouldSplit(ClientContext &context, LogicalComparisonJoin &join, const JoinCondition &common,
                 const JoinCondition &first, const JoinCondition &second) {
	for (auto &child : join.children) {
		if (!child->has_estimated_cardinality || child->estimated_cardinality < STANDARD_VECTOR_SIZE) {
			return false;
		}
	}
	// The join order estimator already considered both inputs. A selective common key on
	// the other input can make rescanning unprofitable even if this scan has few distinct keys.
	auto largest_input = MaxValue(join.children[0]->estimated_cardinality, join.children[1]->estimated_cardinality);
	if (!join.has_estimated_cardinality || join.estimated_cardinality / largest_input < 4) {
		return false;
	}
	for (idx_t side = 0; side < 2; side++) {
		if (join.children[side]->type != LogicalOperatorType::LOGICAL_GET) {
			continue;
		}
		auto &scan = join.children[side]->Cast<LogicalGet>();
		auto get_count = [&](const JoinCondition &cond) {
			return JoinKeyDistinctCount(context, scan, side == 0 ? cond.GetLHS() : cond.GetRHS());
		};
		auto coarse = get_count(common);
		if (coarse && scan.estimated_cardinality / coarse >= 64 && get_count(first) / coarse >= 16 &&
		    get_count(second) / coarse >= 16) {
			return true;
		}
	}
	return false;
}

void TrySplitDistinctJoin(Optimizer &optimizer, LogicalDistinct &distinct) {
	auto &op = distinct.children[0];
	if (distinct.distinct_type != DistinctType::DISTINCT || op->type != LogicalOperatorType::LOGICAL_PROJECTION ||
	    op->children[0]->type != LogicalOperatorType::LOGICAL_COMPARISON_JOIN) {
		return;
	}
	auto &projection = op->Cast<LogicalProjection>();
	auto &join = op->children[0]->Cast<LogicalComparisonJoin>();
	if (join.join_type != JoinType::INNER || join.conditions.size() != 2 ||
	    distinct.distinct_targets.size() != projection.expressions.size()) {
		return;
	}
	// Every output must be a distinct key. Hidden ORDER BY columns and collation keys can
	// otherwise select a representative with first(), whose input would have changed.
	for (auto index : ProjectionIndex::GetIndexes(projection.expressions.size())) {
		auto &target = *distinct.distinct_targets[index];
		if (!IsColumn(*projection.expressions[index]) || !IsColumn(target) ||
		    target.Cast<BoundColumnRefExpression>().Binding() != ColumnBinding(projection.table_index, index)) {
			return;
		}
	}
	const idx_t disjunction = join.conditions[0].IsComparison() ? 1 : 0;
	const auto &common = join.conditions[1 - disjunction];
	const auto &residual = join.conditions[disjunction];
	if (!common.IsComparison() || common.GetComparisonType() != ExpressionType::COMPARE_EQUAL ||
	    !IsColumn(common.GetLHS()) || !IsColumn(common.GetRHS()) || residual.IsComparison() ||
	    residual.GetJoinExpression().GetExpressionType() != ExpressionType::CONJUNCTION_OR ||
	    !RepeatableJoinInput(*join.children[0]) || !RepeatableJoinInput(*join.children[1])) {
		return;
	}
	const auto &terms = residual.GetJoinExpression().Cast<BoundConjunctionExpression>().GetChildren();
	const auto left = join.children[0]->GetColumnBindings();
	const auto right = join.children[1]->GetColumnBindings();
	JoinCondition first, second;
	if (terms.size() != 2 || !GetEqualityKeys(*terms[0], left, right, first) ||
	    !GetEqualityKeys(*terms[1], left, right, second) ||
	    !ShouldSplit(optimizer.context, join, common, first, second)) {
		return;
	}

	// Copy only after matching. The original projection's table index becomes the public union
	// binding; deep copy remaps the other branch's scan and projection bindings independently.
	unique_ptr<LogicalOperator> copy;
	LogicalOperatorDeepCopy cloner(optimizer.binder, nullptr);
	try {
		copy = cloner.DeepCopy(op);
	} catch (NotImplementedException &) {
		return;
	}
	auto &second_join = copy->children[0]->Cast<LogicalComparisonJoin>();
	auto &second_term =
	    *second_join.conditions[disjunction].GetJoinExpression().Cast<BoundConjunctionExpression>().GetChildren()[1];
	JoinCondition copied_equality;
	if (!GetEqualityKeys(second_term, second_join.children[0]->GetColumnBindings(),
	                     second_join.children[1]->GetColumnBindings(), copied_equality)) {
		return;
	}
	second_join.conditions[disjunction] = std::move(copied_equality);
	join.conditions[disjunction] = std::move(first);
	auto result_index = projection.table_index;
	auto count = projection.expressions.size();
	projection.table_index = optimizer.binder.GenerateTableIndex();
	// A pair satisfying both terms appears twice. The existing DISTINCT removes all such overlaps.
	op = make_uniq<LogicalSetOperation>(result_index, count, std::move(op), std::move(copy),
	                                    LogicalOperatorType::LOGICAL_UNION, true);
}

} // namespace

void RewriteDisjunctiveJoins(Optimizer &optimizer, unique_ptr<LogicalOperator> &op) {
	if (op->type == LogicalOperatorType::LOGICAL_SECURE_VIEW ||
	    op->type == LogicalOperatorType::LOGICAL_RECURSIVE_CTE) {
		return;
	}
	for (auto &child : op->children) {
		RewriteDisjunctiveJoins(optimizer, child);
	}
	if (op->type == LogicalOperatorType::LOGICAL_DISTINCT) {
		TrySplitDistinctJoin(optimizer, op->Cast<LogicalDistinct>());
	}
}

} // namespace duckdb
