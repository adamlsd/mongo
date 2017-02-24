// database.h

/**
*    Copyright (C) 2017 10gen Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*    As a special exception, the copyright holders give permission to link the
*    code of portions of this program with the OpenSSL library under certain
*    conditions as described in each individual source file and distribute
*    linked combinations including the program with the OpenSSL library. You
*    must comply with the GNU Affero General Public License in all respects for
*    all of the code used other than as permitted herein. If you modify file(s)
*    with this exception, you may extend this exception to your version of the
*    file(s), but you are not obligated to do so. If you do not wish to do so,
*    delete this exception statement from your version. If you delete this
*    exception statement from all source files in the program, then also delete
*    it in the license file.
*/

#pragma once

#include <memory>
#include <string>

#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/db/catalog/collection_options.h"
#include "mongo/db/catalog/database.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/db/views/view.h"
#include "mongo/db/views/view_catalog.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/string_map.h"

namespace mongo {

class Collection;
class DataFile;
class DatabaseCatalogEntry;
class ExtentManager;
class IndexCatalog;
class NamespaceDetails;
class OperationContext;

/**
 * Represents a logical database containing Collections.
 *
 * The semantics for a const Database are that you can mutate individual collections but not add or
 * remove them.
 */
class DatabaseImpl : public Database::Impl {
public:
    typedef StringMap<Collection*> CollectionMap;

    DatabaseImpl(Database* this_,
                 OperationContext* txn,
                 StringData name,
                 DatabaseCatalogEntry* dbEntry);

    // must call close first
    ~DatabaseImpl() override;

    Database::iterator begin() const override {
        return Database::iterator(_collections.begin());
    }

    Database::iterator end() const override {
        return Database::iterator(_collections.end());
    }

    // closes files and other cleanup see below.
    void close(Database* this_, OperationContext* txn) override;

    const std::string& name() const override {
        return _name;
    }

    void clearTmpCollections(OperationContext* txn) override;

    /**
     * Sets a new profiling level for the database and returns the outcome.
     *
     * @param this_ The `Database` class that holds a pointer to this instance implementing
     *              `Database::Impl`.
     * @param txn Operation context which to use for creating the profiling collection.
     * @param newLevel New profiling level to use.
     */
    Status setProfilingLevel(Database* this_, OperationContext* txn, int newLevel) override;

    int getProfilingLevel() const override {
        return _profile;
    }
    const char* getProfilingNS() const override {
        return _profileName.c_str();
    }

    void getStats(OperationContext* opCtx, BSONObjBuilder* output, double scale = 1) override;

    const DatabaseCatalogEntry* getDatabaseCatalogEntry() const override;

    /**
     * dropCollection() will refuse to drop system collections. Use dropCollectionEvenIfSystem() if
     * that is required.
     */
    Status dropCollection(OperationContext* txn, StringData fullns) override;
    Status dropCollectionEvenIfSystem(OperationContext* txn,
                                      const NamespaceString& fullns) override;

    Status dropView(OperationContext* txn, StringData fullns) override;

    Collection* createCollection(OperationContext* txn,
                                 StringData ns,
                                 const CollectionOptions& options = CollectionOptions(),
                                 bool createDefaultIndexes = true,
                                 const BSONObj& idIndex = BSONObj()) override;

    Status createView(OperationContext* txn,
                      StringData viewName,
                      const CollectionOptions& options) override;

    /**
     * @param ns - this is fully qualified, which is maybe not ideal ???
     */
    Collection* getCollection(StringData ns) const override;

    inline Collection* getCollection(const NamespaceString& ns) const {
        return this->getCollection(ns.ns());
    }

    /**
     * Get the view catalog, which holds the definition for all views created on this database. You
     * must be holding a database lock to use this accessor.
     */
    ViewCatalog* getViewCatalog() override {
        return &_views;
    }

    Collection* getOrCreateCollection(OperationContext* txn, StringData ns) override;

    Status renameCollection(OperationContext* txn,
                            StringData fromNS,
                            StringData toNS,
                            bool stayTemp) override;

    /**
     * Physically drops the specified opened database and removes it from the server's metadata. It
     * doesn't notify the replication subsystem or do any other consistency checks, so it should
     * not be used directly from user commands.
     *
     * Must be called with the specified database locked in X mode.
     */
    //static void dropDatabase(OperationContext* txn, Database* db);

    static Status validateDBName(StringData dbname);

    const std::string& getSystemIndexesName() const override {
        return _indexesName;
    }

    const std::string& getSystemViewsName() const override {
        return _viewsName;
    }

private:
    inline CollectionMap& collections() {
        return this->_collections;
    }

    inline const CollectionMap& collections() const {
        return this->_collections;
    }

    /**
     * Gets or creates collection instance from existing metadata,
     * Returns NULL if invalid
     *
     * Note: This does not add the collection to _collections map, that must be done
     * by the caller, who takes onership of the Collection*
     */
    Collection* _getOrCreateCollectionInstance(OperationContext* txn, StringData fullns);

    /**
     * Throws if there is a reason 'ns' cannot be created as a user collection.
     */
    void _checkCanCreateCollection(const NamespaceString& nss, const CollectionOptions& options);

    /**
     * Deregisters and invalidates all cursors on collection 'fullns'.  Callers must specify
     * 'reason' for why the cache is being cleared.
     */
    void _clearCollectionCache(OperationContext* txn, StringData fullns, const std::string& reason);

    class AddCollectionChange;
    class RemoveCollectionChange;

    const std::string _name;  // "dbname"

    DatabaseCatalogEntry* _dbEntry;  // not owned here

    const std::string _profileName;  // "dbname.system.profile"
    const std::string _indexesName;  // "dbname.system.indexes"
    const std::string _viewsName;    // "dbname.system.views"

    int _profile;  // 0=off.

    CollectionMap _collections;

    DurableViewCatalogImpl _durableViews;  // interface for system.views operations
    ViewCatalog _views;                    // in-memory representation of _durableViews

    friend class Collection;
    friend class NamespaceDetails;
    friend class IndexCatalog;
};
}  // namespace mongo
