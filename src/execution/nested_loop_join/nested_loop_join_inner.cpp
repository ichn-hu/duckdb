#include "execution/nested_loop_join.hpp"
#include "common/operator/comparison_operators.hpp"

using namespace duckdb;
using namespace std;

struct InitialNestedLoopJoin {
	template <class T, class OP>
	static size_t Operation(Vector &left, Vector &right, size_t &lpos, size_t &rpos, sel_t lvector[], sel_t rvector[], size_t current_match_count) {
		// initialize phase of nested loop join
		// fill lvector and rvector with matches from the base vectors
		auto ldata = (T *)left.data;
		auto rdata = (T *)right.data;
		size_t result_count = 0;
		for (; rpos < right.count; rpos++) {
			size_t right_position = right.sel_vector ? right.sel_vector[rpos] : rpos;
			assert(!right.nullmask[right_position]);
			for (; lpos < left.count; lpos++) {
				size_t left_position = left.sel_vector ? left.sel_vector[lpos] : lpos;
				assert(!left.nullmask[left_position]);
				if (OP::Operation(ldata[left_position], rdata[right_position])) {
					// emit tuple
					lvector[result_count] = left_position;
					rvector[result_count] = right_position;
					result_count++;
					if (result_count == STANDARD_VECTOR_SIZE) {
						// out of space!
						return result_count;
					}
				}
			}
			lpos = 0;
		}
		return result_count;
	}
};


struct RefineNestedLoopJoin {
	template <class T, class OP>
	static size_t Operation(Vector &left, Vector &right, size_t &lpos, size_t &rpos, sel_t lvector[], sel_t rvector[], size_t current_match_count) {
		// refine phase of the nested loop join
		// refine lvector and rvector based on matches of subsequent conditions (in case there are multiple conditions in the join)
		assert(current_match_count > 0);
		auto ldata = (T *)left.data;
		auto rdata = (T *)right.data;
		size_t result_count = 0;
		for (size_t i = 0; i < current_match_count; i++) {
			// null values should be filtered out before
			assert(!left.nullmask[lvector[i]] && !right.nullmask[rvector[i]]);
			if (OP::Operation(ldata[lvector[i]], rdata[rvector[i]])) {
				lvector[result_count] = lvector[i];
				rvector[result_count] = rvector[i];
				result_count++;
			}
		}
		return result_count;
	}
};

template <class NLTYPE, class OP>
static size_t nested_loop_join_operator(Vector &left, Vector &right, size_t &lpos, size_t &rpos, sel_t lvector[],
                                        sel_t rvector[], size_t current_match_count) {
	switch (left.type) {
	case TypeId::BOOLEAN:
	case TypeId::TINYINT:
		return NLTYPE::template Operation<int8_t, OP>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	case TypeId::SMALLINT:
		return NLTYPE::template Operation<int16_t, OP>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	case TypeId::DATE:
	case TypeId::INTEGER:
		return NLTYPE::template Operation<int32_t, OP>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	case TypeId::TIMESTAMP:
	case TypeId::BIGINT:
		return NLTYPE::template Operation<int64_t, OP>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	case TypeId::DECIMAL:
		return NLTYPE::template Operation<double, OP>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	case TypeId::POINTER:
		return NLTYPE::template Operation<uint64_t, OP>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	case TypeId::VARCHAR:
		return NLTYPE::template Operation<const char *, OP>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	default:
		throw NotImplementedException("Unimplemented type for join!");
	}
}

template <class NLTYPE>
size_t nested_loop_join(Vector &left, Vector &right, size_t &lpos, size_t &rpos, sel_t lvector[],
                        sel_t rvector[], size_t current_match_count, ExpressionType comparison_type) {
	assert(left.type == right.type);
	switch (comparison_type) {
	case ExpressionType::COMPARE_EQUAL:
		return nested_loop_join_operator<NLTYPE, operators::Equals>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	case ExpressionType::COMPARE_NOTEQUAL:
		return nested_loop_join_operator<NLTYPE, operators::NotEquals>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	case ExpressionType::COMPARE_LESSTHAN:
		return nested_loop_join_operator<NLTYPE, operators::LessThan>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	case ExpressionType::COMPARE_GREATERTHAN:
		return nested_loop_join_operator<NLTYPE, operators::GreaterThan>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	case ExpressionType::COMPARE_LESSTHANOREQUALTO:
		return nested_loop_join_operator<NLTYPE, operators::LessThanEquals>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	case ExpressionType::COMPARE_GREATERTHANOREQUALTO:
		return nested_loop_join_operator<NLTYPE, operators::GreaterThanEquals>(left, right, lpos, rpos, lvector, rvector, current_match_count);
	default:
		throw NotImplementedException("Unimplemented comparison type for join!");
	}
}

size_t NestedLoopJoinInner::Perform(size_t &lpos, size_t &rpos, DataChunk &left_conditions, DataChunk &right_conditions,
	sel_t lvector[], sel_t rvector[], vector<JoinCondition>& conditions) {
	if (lpos >= left_conditions.size() || rpos >= right_conditions.size()) {
		return 0;
	}
	// for the first condition, lvector and rvector are not set yet
	// we initialize them using the InitialNestedLoopJoin
	size_t match_count = nested_loop_join<InitialNestedLoopJoin>(left_conditions.data[0], right_conditions.data[0], lpos, rpos, lvector, rvector, 0, conditions[0].comparison);
	// now resolve the rest of the conditions
	for (size_t i = 1; i < conditions.size(); i++) {
		// check if we have run out of tuples to compare
		if (match_count == 0) {
			return 0;
		}
		// if not, get the vectors to compare
		Vector &l = left_conditions.data[i];
		Vector &r = right_conditions.data[i];
		// then we refine the currently obtained results using the RefineNestedLoopJoin
		match_count = nested_loop_join<RefineNestedLoopJoin>(l, r, lpos, rpos, lvector, rvector, match_count, conditions[i].comparison);
	}
	return match_count;
}
