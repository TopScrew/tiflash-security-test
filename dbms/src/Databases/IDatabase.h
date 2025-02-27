// Copyright 2023 PingCAP, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <Core/NamesAndTypes.h>
#include <Core/Types.h>
#include <Storages/ColumnsDescription.h>
#include <Storages/Transaction/Types.h>

#include <ctime>
#include <functional>
#include <memory>

namespace TiDB
{
struct DBInfo;
using DBInfoPtr = std::shared_ptr<DBInfo>;
} // namespace TiDB

class ThreadPool;


namespace DB
{
class Context;

class IStorage;
using StoragePtr = std::shared_ptr<IStorage>;

class IAST;
using ASTPtr = std::shared_ptr<IAST>;

struct Settings;


/** Allows to iterate over tables.
  */
class IDatabaseIterator
{
public:
    virtual void next() = 0;
    virtual bool isValid() const = 0;

    virtual const String & name() const = 0;
    virtual StoragePtr & table() const = 0;

    virtual ~IDatabaseIterator() = default;
};

using DatabaseIteratorPtr = std::unique_ptr<IDatabaseIterator>;


/** Database engine.
  * It is responsible for:
  * - initialization of set of known tables;
  * - checking existence of a table and getting a table object;
  * - retrieving a list of all tables;
  * - creating and dropping tables;
  * - renaming tables and moving between databases with same engine.
  */

class IDatabase : public std::enable_shared_from_this<IDatabase>
{
public:
    /// Get name of database engine.
    virtual String getEngineName() const = 0;

    /// Load a set of existing tables. If thread_pool is specified, use it.
    /// You can call only once, right after the object is created.
    virtual void loadTables(Context & context, ThreadPool * thread_pool, bool has_force_restore_data_flag) = 0;

    /// Check the existence of the table.
    virtual bool isTableExist(const Context & context, const String & name) const = 0;

    /// Get the table for work. Return nullptr if there is no table.
    virtual StoragePtr tryGetTable(const Context & context, const String & name) const = 0;

    /// Get an iterator that allows you to pass through all the tables.
    /// It is possible to have "hidden" tables that are not visible when passing through, but are visible if you get them by name using the functions above.
    virtual DatabaseIteratorPtr getIterator(const Context & context) = 0;

    /// Is the database empty.
    virtual bool empty(const Context & context) const = 0;

    /// Add the table to the database. Record its presence in the metadata.
    virtual void createTable(const Context & context, const String & name, const StoragePtr & table, const ASTPtr & query) = 0;

    /// Delete the table from the database and return it. Delete the metadata.
    virtual void removeTable(const Context & context, const String & name) = 0;

    /// Add a table to the database, but do not add it to the metadata. The database may not support this method.
    virtual void attachTable(const String & name, const StoragePtr & table) = 0;

    /// Forget about the table without deleting it, and return it. The database may not support this method.
    virtual StoragePtr detachTable(const String & name) = 0;

    /// Rename the table and possibly move the table to another database.
    virtual void renameTable(const Context & context, const String & name, IDatabase & to_database, const String & to_name) = 0;

    using ASTModifier = std::function<void(IAST &)>;

    /// Change the table structure in metadata.
    /// You must call under the TableStructureLock of the corresponding table . If engine_modifier is empty, then engine does not change.
    virtual void alterTable(
        const Context & context,
        const String & name,
        const ColumnsDescription & columns,
        const ASTModifier & engine_modifier)
        = 0;

    /// Returns time of table's metadata change, 0 if there is no corresponding metadata file.
    virtual time_t getTableMetadataModificationTime(const Context & context, const String & name) = 0;

    /// Get the CREATE TABLE query for the table. It can also provide information for detached tables for which there is metadata.
    virtual ASTPtr tryGetCreateTableQuery(const Context & context, const String & name) const = 0;

    virtual ASTPtr getCreateTableQuery(const Context & context, const String & name) const { return tryGetCreateTableQuery(context, name); }

    /// Get the CREATE DATABASE query for current database.
    virtual ASTPtr getCreateDatabaseQuery(const Context & context) const = 0;

    /// Returns path for persistent data storage if the database supports it, empty string otherwise
    virtual String getDataPath() const { return {}; }
    /// Returns metadata path if the database supports it, empty string otherwise
    virtual String getMetadataPath() const { return {}; }
    /// Returns metadata path of a concrete table if the database supports it, empty string otherwise
    virtual String getTableMetadataPath(const String & /*table_name*/) const { return {}; }

    /// Ask all tables to complete the background threads they are using and delete all table objects.
    virtual void shutdown() = 0;

    virtual bool isTombstone() const { return false; }
    virtual Timestamp getTombstone() const { return 0; }
    virtual void alterTombstone(const Context & /*context*/, Timestamp /*tombstone_*/, const TiDB::DBInfoPtr & /*new_db_info*/) {}

    /// Delete metadata, the deletion of which differs from the recursive deletion of the directory, if any.
    virtual void drop(const Context & context) = 0;

    virtual ~IDatabase() = default;
};

using DatabasePtr = std::shared_ptr<IDatabase>;
using Databases = std::map<String, DatabasePtr>;

} // namespace DB
