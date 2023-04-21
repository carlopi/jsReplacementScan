#define DUCKDB_EXTENSION_MAIN

#include "js_replacement_scan_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include <emscripten.h>
#include "duckdb/catalog/catalog.hpp"
#include <vector>

#include "duckdb/common/constants.hpp"
#include "duckdb/common/enums/file_compression_type.hpp"
#include "duckdb/common/field_writer.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/types/chunk_collection.hpp"
#include "duckdb/function/copy_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/parsed_data/create_copy_function_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/storage/statistics/base_statistics.hpp"
#include "duckdb/common/multi_file_reader.hpp"
#include "duckdb/storage/table/row_group.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

#include "duckdb/catalog/catalog_entry/table_function_catalog_entry.hpp"

namespace duckdb {

static unique_ptr<TableRef> JavaScriptWrapperScanReplacement(ClientContext &context, const string &table_name,
                                            ReplacementScanData *data) {
	auto lower_name = StringUtil::Lower(table_name);


	char *str = (char*)EM_ASM_PTR({
		var tableNameJS = UTF8ToString($0);
		
		if (typeof myVeryOwnReplacementScanFunction !== 'function')
			return 0;
		
		var res = myVeryOwnReplacementScanFunction(tableNameJS);
		if (!res)
			return 0;

		var output = res.function + " " + res.table;
		var lengthBytes = lengthBytesUTF8(output) + 1;
		var stringOnWasmHeap = _malloc(lengthBytes);
		stringToUTF8(output, stringOnWasmHeap, lengthBytes);
		return stringOnWasmHeap;
	}, table_name.c_str());

	if (str == nullptr)
		return nullptr;
	
	std::string res(str);
	free(str);
	std::string function_name = res.substr(0, res.find(' '));
	std::string file_name = res.substr(res.find(' ')+1);
	
	auto table_function = make_uniq<TableFunctionRef>();
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(make_uniq<ConstantExpression>(Value(file_name)));
	table_function->function = make_uniq<FunctionExpression>(function_name, std::move(children));
	return std::move(table_function);
}

static void LoadInternal(DatabaseInstance &instance) {
	EM_ASM({
		try {
			// Either full URL or relative to duckdb-eh.js location, CORS enabled
			importScripts("customReplacementScan.js");
		}
		catch(ex)
		{
			console.log(ex);
		}
	});

	auto &config = DBConfig::GetConfig(instance);
	config.replacement_scans.emplace_back(JavaScriptWrapperScanReplacement);
}

void CustomReplacementScanExtension::Load(DuckDB &db) {
	LoadInternal(*db.instance);
}
std::string CustomReplacementScanExtension::Name() {
	return "custom_replacement_scan";
}

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void custom_replacement_scan_init(duckdb::DatabaseInstance &db) {
	LoadInternal(db);
}

DUCKDB_EXTENSION_API const char *custom_replacement_scan_version() {
	return duckdb::DuckDB::LibraryVersion();
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
