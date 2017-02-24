// database.h

/**
*    Copyright (C) 2017 MongoDB Inc.
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
#include "mongo/db/namespace_string.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/db/views/view.h"
#include "mongo/db/views/view_catalog.h"
#include "mongo/stdx/functional.h"
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
class Database {
public:
    typedef StringMap<Collection*> CollectionMap;

    /**
     * Iterating over a Database yields Collection* pointers.
     */
    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Collection*;
        using pointer = const value_type*;
        using reference = const value_type&;
        using difference_type = ptrdiff_t;

        iterator() = default;

        explicit iterator(CollectionMap::const_iterator it) : _it(it) {}

        reference operator*() const {
            return _it->second;
        }

        pointer operator->() const {
            return &_it->second;
        }

        bool operator==(const iterator& other) {
            return _it == other._it;
        }

        bool operator!=(const iterator& other) {
            return _it != other._it;
        }

        iterator& operator++() {
            ++_it;
            return *this;
        }

        iterator operator++(int) {
            auto oldPosition = *this;
            ++_it;
            return oldPosition;
        }

    private:
        CollectionMap::const_iterator _it;
    };

    class Impl {
    public:
        virtual ~Impl();

        virtual iterator begin() const = 0;

        virtual iterator end() const = 0;

        // closes files and other cleanup see below.
        virtual void close(Database* this_, OperationContext* txn) = 0;

        virtual const std::string& name() const = 0;

        virtual void clearTmpCollections(OperationContext* txn) = 0;

        virtual Status setProfilingLevel(Database* this_, OperationContext* txn, int newLevel) = 0;

        virtual int getProfilingLevel() const = 0;

        virtual const char* getProfilingNS() const = 0;

        virtual void getStats(OperationContext* opCtx, BSONObjBuilder* output, double scale) = 0;

        virtual const DatabaseCatalogEntry* getDatabaseCatalogEntry() const = 0;

        virtual Status dropCollection(OperationContext* txn, StringData fullns) = 0;

        virtual Status dropCollectionEvenIfSystem(OperationContext* txn,
                                                  const NamespaceString& fullns) = 0;

        virtual Status dropView(OperationContext* txn, StringData fullns) = 0;

        virtual Collection* createCollection(OperationContext* txn,
                                             StringData ns,
                                             const CollectionOptions& options,
                                             bool createDefaultIndexes,
                                             const BSONObj& idIndex) = 0;

        virtual CollectionMap& collections() = 0;

        virtual const CollectionMap& collections() const = 0;

        virtual Status createView(OperationContext* txn,
                                  StringData viewName,
                                  const CollectionOptions& options) = 0;

        virtual Collection* getCollection(StringData ns) const = 0;

        virtual ViewCatalog* getViewCatalog() = 0;

        virtual Collection* getOrCreateCollection(OperationContext* txn, StringData ns) = 0;

        virtual Status renameCollection(OperationContext* txn,
                                        StringData fromNS,
                                        StringData toNS,
                                        bool stayTemp) = 0;

        virtual const std::string& getSystemIndexesName() const = 0;

        virtual const std::string& getSystemViewsName() const = 0;
    };

    friend class DatabaseImpl;

private:
    std::unique_ptr<Impl> _pimpl;

    Impl* pimpl();

    const Impl* pimpl() const;

    static std::unique_ptr<Impl> makeImpl(Database* this_,
                                          OperationContext* txn,
                                          StringData name,
                                          DatabaseCatalogEntry* dbEntry);

public:
    static void registerImpl(
        stdx::function<std::unique_ptr<Impl>(
            Database*, OperationContext*, StringData, DatabaseCatalogEntry*)> factory);

    // must call close first
    inline ~Database() = default;

    inline Database(Database&& copy) = default;

    inline Database& operator=(Database&& copy) = default;

    explicit inline Database(OperationContext* const txn,
                             const StringData name,
                             DatabaseCatalogEntry* const dbEntry)
        : _pimpl(makeImpl(this, txn, name, dbEntry)) {}


    inline iterator begin() const {
        return this->pimpl()->begin();
    }

    inline iterator end() const {
        return this->pimpl()->end();
    }

    // closes files and other cleanup see below.
    inline void close(OperationContext* const txn) {
        return this->pimpl()->close(this, txn);
    }

    inline const std::string& name() const {
        return this->pimpl()->name();
    }

    inline void clearTmpCollections(OperationContext* const txn) {
        return this->pimpl()->clearTmpCollections(txn);
    }

    /**
     * Sets a new profiling level for the database and returns the outcome.
     *
     * @param txn Operation context which to use for creating the profiling collection.
     * @param newLevel New profiling level to use.
     */
    inline Status setProfilingLevel(OperationContext* const txn, const int newLevel) {
        return this->pimpl()->setProfilingLevel(this, txn, newLevel);
    }

    inline int getProfilingLevel() const {
        return this->pimpl()->getProfilingLevel();
    }

    inline const char* getProfilingNS() const {
        return this->pimpl()->getProfilingNS();
    }

    inline void getStats(OperationContext* const opCtx,
                         BSONObjBuilder* const output,
                         const double scale = 1) {
        return this->pimpl()->getStats(opCtx, output, scale);
    }

    inline const DatabaseCatalogEntry* getDatabaseCatalogEntry() const {
        return this->pimpl()->getDatabaseCatalogEntry();
    }

    /**
     * dropCollection() will refuse to drop system collections. Use dropCollectionEvenIfSystem() if
     * that is required.
     */
    inline Status dropCollection(OperationContext* const txn, const StringData fullns) {
        return this->pimpl()->dropCollection(txn, fullns);
    }
    inline Status dropCollectionEvenIfSystem(OperationContext* const txn,
                                             const NamespaceString& fullns) {
        return this->pimpl()->dropCollectionEvenIfSystem(txn, fullns);
    }

    inline Status dropView(OperationContext* const txn, const StringData fullns) {
        return this->pimpl()->dropView(txn, fullns);
    }

    inline Collection* createCollection(OperationContext* const txn,
                                        const StringData ns,
                                        const CollectionOptions& options = CollectionOptions(),
                                        const bool createDefaultIndexes = true,
                                        const BSONObj& idIndex = BSONObj()) {
        return this->pimpl()->createCollection(txn, ns, options, createDefaultIndexes, idIndex);
    }

    inline Status createView(OperationContext* const txn,
                             const StringData viewName,
                             const CollectionOptions& options) {
        return this->pimpl()->createView(txn, viewName, options);
    }

    /**
     * @param ns - this is fully qualified, which is maybe not ideal ???
     */
    inline Collection* getCollection(const StringData ns) const {
        return this->pimpl()->getCollection(ns);
    }

    inline Collection* getCollection(const NamespaceString& ns) const {
        return this->getCollection(ns.ns());
    }

    /**
     * Get the view catalog, which holds the definition for all views created on this database. You
     * must be holding a database lock to use this accessor.
     */
    inline ViewCatalog* getViewCatalog() {
        return this->pimpl()->getViewCatalog();
    }

    inline Collection* getOrCreateCollection(OperationContext* const txn, const StringData ns) {
        return this->pimpl()->getOrCreateCollection(txn, ns);
    }

    inline Status renameCollection(OperationContext* const txn,
                                   const StringData fromNS,
                                   const StringData toNS,
                                   const bool stayTemp) {
        return this->pimpl()->renameCollection(txn, fromNS, toNS, stayTemp);
    }

    /**
     * Physically drops the specified opened database and removes it from the server's metadata. It
     * doesn't notify the replication subsystem or do any other consistency checks, so it should
     * not be used directly from user commands.
     *
     * Must be called with the specified database locked in X mode.
     */
    static void dropDatabase(OperationContext* txn, Database* db);

    static Status validateDBName(StringData dbname);

    inline const std::string& getSystemIndexesName() const {
        return this->pimpl()->getSystemIndexesName();
    }

    const std::string& getSystemViewsName() const {
        return this->pimpl()->getSystemViewsName();
    }

private:
    inline CollectionMap& collections() {
        return this->pimpl()->collections();
    }

    inline const CollectionMap& collections() const {
        return this->pimpl()->collections();
    }
};

/**
 * Creates the namespace 'ns' in the database 'db' according to 'options'. If 'createDefaultIndexes'
 * is true, creates the _id index for the collection (and the system indexes, in the case of system
 * collections). Creates the collection's _id index according to 'idIndex', if it is non-empty. When
 * 'idIndex' is empty, creates the default _id index.
 */
Status userCreateNS(OperationContext* txn,
                    Database* db,
                    StringData ns,
                    BSONObj options,
                    bool createDefaultIndexes = true,
                    const BSONObj& idIndex = BSONObj());

void registerUserCreateNSHandler(
    stdx::function<Status(OperationContext*, Database*, StringData, BSONObj, bool, const BSONObj&)>
        handler);

void dropAllDatabasesExceptLocal(OperationContext* txn);

void registerDropAllDatabasesExceptLocalHandler(stdx::function<void(OperationContext*)> handler);
}  // namespace mongo
