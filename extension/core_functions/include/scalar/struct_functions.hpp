//===----------------------------------------------------------------------===//
//                         DuckDB
//
// scalar/struct_functions.hpp
//
//
//===----------------------------------------------------------------------===//
// This file is generated by scripts/generate_functions.py

#pragma once

#include "duckdb/function/function_set.hpp"

namespace duckdb {

struct StructInsertFun {
	static constexpr const char *Name = "struct_insert";
	static constexpr const char *Parameters = "struct,any";
	static constexpr const char *Description = "Add field(s)/value(s) to an existing STRUCT with the argument values. The entry name(s) will be the bound variable name(s).";
	static constexpr const char *Example = "struct_insert({'a': 1}, b := 2)";

	static ScalarFunction GetFunction();
};

struct StructPackFun {
	static constexpr const char *Name = "struct_pack";
	static constexpr const char *Parameters = "any";
	static constexpr const char *Description = "Create a STRUCT containing the argument values. The entry name will be the bound variable name";
	static constexpr const char *Example = "struct_pack(i := 4, s := 'string')";

	static ScalarFunction GetFunction();
};

struct RowFun {
	using ALIAS = StructPackFun;

	static constexpr const char *Name = "row";
};

} // namespace duckdb
