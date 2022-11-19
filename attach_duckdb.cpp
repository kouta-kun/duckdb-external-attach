#include <stdint.h>
#include "duckdb.hpp"
#include "duckdb/transaction/transaction_manager.hpp"
#include "duckdb/main/query_result.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/expression/cast_expression.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include <cmath>
/*
 * external duckdb_attachment
 * Copyright (c) 2022, Salvador Pardiñas <darkfm@vera.com.uy>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

namespace duckdb {

// this is private in query_result for some reason, I'd rather include it from there but for now I'll do this
namespace __copied_from_query_result_hpp {
class QueryResultIterator;
class QueryResultRow {
public:
	explicit QueryResultRow(QueryResultIterator &iterator_p, idx_t row_idx) : iterator(iterator_p), row(0) {
	}

	QueryResultIterator &iterator;
	idx_t row;

	Value GetValue(idx_t col_idx) const;
};
//! The row-based query result iterator. Invoking the
class QueryResultIterator {
public:
	explicit QueryResultIterator(QueryResult *result_p) : current_row(*this, 0), result(result_p), base_row(0) {
		if (result) {
			chunk = shared_ptr<DataChunk>(result->Fetch().release());
			if (!chunk) {
				result = nullptr;
			}
		}
	}

	QueryResultRow current_row;
	shared_ptr<DataChunk> chunk;
	QueryResult *result;
	idx_t base_row;

public:
	void Next() {
		if (!chunk) {
			return;
		}
		current_row.row++;
		if (current_row.row >= chunk->size()) {
			base_row += chunk->size();
			chunk = result->Fetch();
			current_row.row = 0;
			if (!chunk || chunk->size() == 0) {
				// exhausted all rows
				base_row = 0;
				result = nullptr;
				chunk.reset();
			}
		}
	}

	QueryResultIterator &operator++() {
		Next();
		return *this;
	}
	bool operator!=(const QueryResultIterator &other) const {
		return result != other.result || base_row != other.base_row || current_row.row != other.current_row.row;
	}
	const QueryResultRow &operator*() const {
		return current_row;
	}
};

Value QueryResultRow::GetValue(idx_t col_idx) const {
	return iterator.chunk->GetValue(col_idx, row);
}
} // namespace __copied_from_query_result_hpp
using QueryResultIterator = __copied_from_query_result_hpp::QueryResultIterator;

struct ExternalQueryFunctionData : public TableFunctionData {
	ExternalQueryFunctionData() {
	}

	bool finished = false;
	string file_name = "";
	string table = "";
	unique_ptr<DuckDB> database = nullptr;
	unique_ptr<Connection> connection = nullptr;
	unique_ptr<QueryResult> tablerelation = nullptr;
	unique_ptr<QueryResultIterator> rowiterator = nullptr;
};

static unique_ptr<FunctionData> ExternalQueryBind(ClientContext &context, TableFunctionBindInput &input,
                                                  vector<LogicalType> &return_types, vector<string> &names) {

	auto result = make_unique<ExternalQueryFunctionData>();
	result->file_name = input.inputs[0].GetValue<string>();
	result->table = input.inputs[1].GetValue<string>();

	result->database = make_unique<DuckDB>(result->file_name);
	result->connection = make_unique<Connection>(*result->database);

	result->tablerelation = result->connection->Table(result->table)->Execute();

	for (idx_t i = 0; i < result->tablerelation->names.size(); i++) {
		return_types.push_back(result->tablerelation->types[i]);
		names.emplace_back(result->tablerelation->names[i]);
	}
	return move(result);
}

static void ExternalQueryFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &data = (ExternalQueryFunctionData &)*data_p.bind_data;
	if (data.finished) {
		return;
	}

	if (data.rowiterator == nullptr) {
		data.rowiterator = make_unique<QueryResultIterator>(data.tablerelation.get());
	}

	auto dconn = Connection(context.db->GetDatabase(context));

	idx_t count = 0;

	while ((data.rowiterator)->result != nullptr && count < STANDARD_VECTOR_SIZE) {
		for (idx_t col = 0; col < data.tablerelation->types.size(); col++) {
			output.SetValue(col, count, (*(*(data.rowiterator))).GetValue(col));
		}
		count++;
		data.rowiterator->Next();
	}
	if ((data.rowiterator)->result == nullptr) {
		data.finished = true;
	}
	// todo copy views
	// {
	// 	check_ok(sqlite3_prepare_v2(db, "SELECT sql FROM sqlite_master WHERE type='view'", -1, &res, nullptr), db);

	// 	while (sqlite3_step(res) == SQLITE_ROW) {
	// 		auto view_sql = string((const char *)sqlite3_column_text(res, 0));
	// 		dconn.Query(view_sql);
	// 	}
	// 	check_ok(sqlite3_finalize(res), db);
	// }
	output.SetCardinality(count);
}

struct AttachFunctionData : public TableFunctionData {
	AttachFunctionData() {
	}

	bool finished = false;
	bool overwrite = false;
	bool temporary = false;
	string file_name = "";
};

static unique_ptr<FunctionData> AttachBind(ClientContext &context, TableFunctionBindInput &input,
                                           vector<LogicalType> &return_types, vector<string> &names) {

	auto result = make_unique<AttachFunctionData>();
	result->file_name = input.inputs[0].GetValue<string>();

	for (auto &kv : input.named_parameters) {
		if (kv.first == "overwrite") {
			result->overwrite = BooleanValue::Get(kv.second);
		}
		if (kv.first == "temporary") {
			result->temporary = BooleanValue::Get(kv.second);
		}
	}

	return_types.push_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");
	return move(result);
}

static void AttachFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &data = (AttachFunctionData &)*data_p.bind_data;
	if (data.finished) {
		return;
	}

	DuckDB db {data.file_name};
	auto econn = Connection(db);
	auto dconn = Connection(context.db->GetDatabase(context));

	{

		auto q = econn.Query("select table_name from duckdb_tables();");
		for (auto &row : *q) {
			auto table_name = row.GetValue<string>(0);

			auto table = econn.Table(table_name)->Execute();

			vector<vector<Value>> table_values;
			unique_ptr<DataChunk> trow = nullptr;
			while ((trow = table->Fetch()) != nullptr) {
				for (idx_t row_idx = 0; row_idx < trow->size(); row_idx++) {
					vector<Value> row_values;
					for (idx_t col = 0; col < table->names.size(); col++) {
						row_values.push_back(trow->GetValue(col, row_idx));
					}
					table_values.push_back(row_values);
				}
			}

			dconn.Values(table_values, table->names, table_name)
			    ->CreateView(table_name, data.overwrite, data.temporary);
		}
	}
	// todo copy views
	// {
	// 	check_ok(sqlite3_prepare_v2(db, "SELECT sql FROM sqlite_master WHERE type='view'", -1, &res, nullptr), db);

	// 	while (sqlite3_step(res) == SQLITE_ROW) {
	// 		auto view_sql = string((const char *)sqlite3_column_text(res, 0));
	// 		dconn.Query(view_sql);
	// 	}
	// 	check_ok(sqlite3_finalize(res), db);
	// }
	data.finished = true;
}

}; // namespace duckdb
extern "C" {
  using namespace duckdb;
DUCKDB_EXTENSION_API void attach_extension_init(duckdb::DatabaseInstance &db) {
	auto &catalog = db.GetCatalog();

	TableFunction attach_duckdb("attach_external", {LogicalType::VARCHAR}, AttachFunction, AttachBind);
	attach_duckdb.named_parameters["overwrite"] = LogicalType::BOOLEAN;
	attach_duckdb.named_parameters["temporary"] = LogicalType::BOOLEAN;

	CreateTableFunctionInfo attach_duckdb_info {attach_duckdb};

	auto client_context = std::make_shared<ClientContext>(db.shared_from_this());
	client_context->transaction.BeginTransaction();

	catalog.CreateTableFunction(*client_context, &attach_duckdb_info);

	TableFunction query_external_duckdb("query_external", {LogicalType::VARCHAR, LogicalType::VARCHAR},
					    ExternalQueryFunction, ExternalQueryBind);
	CreateTableFunctionInfo query_external_info {query_external_duckdb};

	catalog.CreateTableFunction(*client_context, &query_external_info);
	client_context->transaction.Commit();
}

DUCKDB_EXTENSION_API const char *attach_extension_version() {
	return DuckDB::LibraryVersion();
}
// static void RegisterAttachFunction(BuiltinFunctions &set) {
// 	TableFunction attach_duckdb("attach_external", {LogicalType::VARCHAR}, AttachFunction, AttachBind);
// 	attach_duckdb.named_parameters["overwrite"] = LogicalType::BOOLEAN;
// 	attach_duckdb.named_parameters["temporary"] = LogicalType::BOOLEAN;
// 	set.AddFunction(attach_duckdb);

// 	TableFunction query_external_duckdb("query_external", {LogicalType::VARCHAR, LogicalType::VARCHAR},
// 	                                    ExternalQueryFunction, ExternalQueryBind);
// 	set.AddFunction(query_external_duckdb);
// }

// void BuiltinFunctions::RegisterAttachFunctions() {
// 	RegisterAttachFunction(*this);
// }
}
