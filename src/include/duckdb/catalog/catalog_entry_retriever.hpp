//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/catalog/catalog_entry_retriever.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include "duckdb/common/enums/catalog_type.hpp"
#include "duckdb/common/enums/on_entry_not_found.hpp"
#include "duckdb/common/string.hpp"
#include "duckdb/parser/query_error_context.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_search_path.hpp"
#include "duckdb/catalog/entry_lookup_info.hpp"

namespace duckdb {

class ClientContext;
class Catalog;
class CatalogEntry;

using catalog_entry_callback_t = std::function<void(CatalogEntry &)>;

class StandardEntry;

//! Hardcharger: where the functions, macros and types a stored expression names are found (a view's
//! query, a column default, a CHECK constraint, a generated column, a trigger's body, a policy): this
//! schema and DuckDB's own, whatever the session. server/scripts/patch-duckdb-name-scope.cmake.
struct HardchargerNameScope {
	HardchargerNameScope(SchemaCatalogEntry &schema_p, string kind_p, string object_p)
	    : schema(schema_p), kind(std::move(kind_p)), object(std::move(object_p)) {
	}
	//! The scope of a table's or view's own expressions (null while no hook is set)
	static shared_ptr<const HardchargerNameScope> Of(const StandardEntry &entry, const char *kind);

	//! The prefix of the marker call that carries a scope with a spliced expression (Binder::HardchargerScoped)
	static constexpr const char *MARKER = "__hardcharger_name_scope_";

	SchemaCatalogEntry &schema;
	//! What the expression belongs to, for messages: "table", "view", "trigger", "policy"; and its name
	string kind;
	string object;
};

//! Answers a lookup made under a scope. Returns false for a lookup that is not its to answer (DuckDB's
//! own lookup follows); true with the entry (or null, where the caller takes null for "not found").
//! Null unless Hardcharger sets it.
extern bool (*hardcharger_scoped_lookup_hook)(ClientContext &context, const HardchargerNameScope &scope,
                                              const EntryLookupInfo &lookup_info, OnEntryNotFound on_entry_not_found,
                                              optional_ptr<CatalogEntry> &result);

// Wraps the Catalog::GetEntry method
class CatalogEntryRetriever {
public:
	explicit CatalogEntryRetriever(ClientContext &context) : context(context) {
	}
	CatalogEntryRetriever(const CatalogEntryRetriever &other) : callback(other.callback), context(other.context) {
	}

public:
	void Inherit(const CatalogEntryRetriever &parent);
	ClientContext &GetContext() {
		return context;
	}

	//! Look up an entry described by the (catalog/schema-qualified) EntryLookupInfo
	optional_ptr<CatalogEntry> GetEntry(const EntryLookupInfo &lookup_info,
	                                    OnEntryNotFound on_entry_not_found = OnEntryNotFound::THROW_EXCEPTION);
	//! Look up a (catalog/schema-qualified) type
	LogicalType GetType(const QualifiedName &name, OnEntryNotFound on_entry_not_found = OnEntryNotFound::RETURN_NULL);
	//! Look up the schema described by the (catalog-qualified) EntryLookupInfo
	optional_ptr<SchemaCatalogEntry> GetSchema(const EntryLookupInfo &schema_lookup,
	                                           OnEntryNotFound on_entry_not_found = OnEntryNotFound::THROW_EXCEPTION);

	//! Deprecated: the catalog/schema qualification is now carried inside the EntryLookupInfo / QualifiedName - fold it
	//! in there instead of passing it separately.
	[[deprecated(
	    "Fold catalog/schema into the EntryLookupInfo and use GetEntry(EntryLookupInfo)")]] optional_ptr<CatalogEntry>
	GetEntry(const Identifier &catalog, const Identifier &schema, const EntryLookupInfo &lookup_info,
	         OnEntryNotFound on_entry_not_found = OnEntryNotFound::THROW_EXCEPTION);
	[[deprecated(
	    "Fold the schema into the EntryLookupInfo and use GetEntry(EntryLookupInfo)")]] optional_ptr<CatalogEntry>
	GetEntry(Catalog &catalog, const Identifier &schema, const EntryLookupInfo &lookup_info,
	         OnEntryNotFound on_entry_not_found = OnEntryNotFound::THROW_EXCEPTION);
	[[deprecated("Fold catalog/schema into the QualifiedName and use GetType(QualifiedName)")]] LogicalType
	GetType(const Identifier &catalog, const Identifier &schema, const Identifier &name,
	        OnEntryNotFound on_entry_not_found = OnEntryNotFound::RETURN_NULL);
	[[deprecated("Fold the schema into the QualifiedName and use GetType(QualifiedName)")]] LogicalType
	GetType(Catalog &catalog, const Identifier &schema, const Identifier &name,
	        OnEntryNotFound on_entry_not_found = OnEntryNotFound::RETURN_NULL);
	[[deprecated("Fold the catalog into the EntryLookupInfo and use GetSchema(EntryLookupInfo)")]] optional_ptr<
	    SchemaCatalogEntry>
	GetSchema(const Identifier &catalog, const EntryLookupInfo &schema_lookup,
	          OnEntryNotFound on_entry_not_found = OnEntryNotFound::THROW_EXCEPTION);

	const CatalogSearchPath &GetSearchPath() const;
	void SetSearchPath(vector<CatalogSearchEntry> entries);

	void SetCallback(catalog_entry_callback_t callback);
	//! Hardcharger: the name scope of the stored expression this retriever binds for (null: none)
	const shared_ptr<const HardchargerNameScope> &GetNameScope() const {
		return name_scope;
	}
	//! Sets it, and returns the one it had
	shared_ptr<const HardchargerNameScope> SetNameScope(shared_ptr<const HardchargerNameScope> scope);
	catalog_entry_callback_t GetCallback();

	optional_ptr<BoundAtClause> GetAtClause() const;
	void SetAtClause(optional_ptr<BoundAtClause> at_clause);

private:
	optional_ptr<CatalogEntry> ReturnAndCallback(optional_ptr<CatalogEntry> result);

private:
	//! (optional) callback, called on every successful entry retrieval
	catalog_entry_callback_t callback = nullptr;
	ClientContext &context;
	shared_ptr<CatalogSearchPath> search_path;
	optional_ptr<BoundAtClause> at_clause;
	shared_ptr<const HardchargerNameScope> name_scope;
};

//! Hardcharger: binds under `scope` until it goes out of (C++) scope; a null scope changes nothing
class HardchargerNameScopeGuard {
public:
	HardchargerNameScopeGuard(CatalogEntryRetriever &retriever_p, shared_ptr<const HardchargerNameScope> scope)
	    : retriever(retriever_p), active(scope != nullptr) {
		if (active) {
			previous = retriever.SetNameScope(std::move(scope));
		}
	}
	~HardchargerNameScopeGuard() {
		if (active) {
			retriever.SetNameScope(std::move(previous));
		}
	}
	HardchargerNameScopeGuard(const HardchargerNameScopeGuard &) = delete;
	HardchargerNameScopeGuard &operator=(const HardchargerNameScopeGuard &) = delete;

private:
	CatalogEntryRetriever &retriever;
	bool active;
	shared_ptr<const HardchargerNameScope> previous;
};

} // namespace duckdb
