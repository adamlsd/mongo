// index_catalog_entry.h

/**
*    Copyright (C) 2013 10gen Inc.
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

#include <boost/optional.hpp>
#include <string>

#include "mongo/base/owned_pointer_vector.h"
#include "mongo/bson/ordering.h"
#include "mongo/db/index/multikey_paths.h"
#include "mongo/db/record_id.h"
#include "mongo/db/storage/snapshot_name.h"
#include "mongo/platform/atomic_word.h"
#include "mongo/stdx/functional.h"
#include "mongo/stdx/mutex.h"

namespace mongo {
class CollatorInterface;
class CollectionCatalogEntry;
class CollectionInfoCache;
class HeadManager;
class IndexDescriptor;
class MatchExpression;
class OperationContext;
class IndexAccessMethod;

class IndexCatalogEntry {
public:
    class Impl {
    public:
        virtual ~Impl();

        virtual const std::string& ns() const = 0;

        virtual void init(std::unique_ptr<IndexAccessMethod> accessMethod) = 0;

        virtual IndexDescriptor* descriptor() = 0;

        virtual const IndexDescriptor* descriptor() const = 0;

        virtual IndexAccessMethod* accessMethod() = 0;

        virtual const IndexAccessMethod* accessMethod() const = 0;

        virtual const Ordering& ordering() const = 0;

        virtual const MatchExpression* getFilterExpression() const = 0;

        virtual const CollatorInterface* getCollator() const = 0;

        virtual const RecordId& head(OperationContext* txn) const = 0;

        virtual void setHead(OperationContext* txn, RecordId newHead) = 0;

        virtual void setIsReady(bool newIsReady) = 0;

        virtual HeadManager* headManager() const = 0;

        virtual bool isMultikey() const = 0;

        virtual MultikeyPaths getMultikeyPaths(OperationContext* txn) const = 0;

        virtual void setMultikey(OperationContext* txn, const MultikeyPaths& multikeyPaths) = 0;

        virtual bool isReady(OperationContext* txn) const = 0;

        virtual boost::optional<SnapshotName> getMinimumVisibleSnapshot() = 0;

        virtual void setMinimumVisibleSnapshot(SnapshotName name) = 0;
    };

private:
    std::unique_ptr<Impl> _pimpl;

	const Impl *pimpl() const;
	Impl *pimpl();

    static std::unique_ptr<Impl> makeImpl(OperationContext* txn,
                                          StringData ns,
                                          CollectionCatalogEntry* collection,  // not owned
                                          IndexDescriptor* descriptor,  // ownership passes to me
                                          CollectionInfoCache* infoCache);  // not owned, optional

public:
    static void registerFactory(
        stdx::function<std::unique_ptr<Impl>(OperationContext*,
                                             StringData,
                                             CollectionCatalogEntry*,
                                             IndexDescriptor*,
                                             CollectionInfoCache* infoCache)>);

    IndexCatalogEntry(OperationContext* txn,
                      StringData ns,
                      CollectionCatalogEntry* collection,  // not owned
                      IndexDescriptor* descriptor,         // ownership passes to me
                      CollectionInfoCache* infoCache);     // not owned, optional

    ~IndexCatalogEntry();

    const std::string& ns() const {
        return this->pimpl()->ns();
    }

    void init(std::unique_ptr<IndexAccessMethod> accessMethod);

    IndexDescriptor* descriptor() {
        return this->pimpl()->descriptor();
    }

    const IndexDescriptor* descriptor() const {
        return this->pimpl()->descriptor();
    }

    IndexAccessMethod* accessMethod() {
        return this->pimpl()->accessMethod();
    }

    const IndexAccessMethod* accessMethod() const {
        return this->pimpl()->accessMethod();
    }

    const Ordering& ordering() const {
        return this->pimpl()->ordering();
    }

    const MatchExpression* getFilterExpression() const {
        return this->pimpl()->getFilterExpression();
    }

    const CollatorInterface* getCollator() const {
        return this->pimpl()->getCollator();
    }

    /// ---------------------

    const RecordId& head(OperationContext* const txn) const {
        return this->pimpl()->head(txn);
    }

    void setHead(OperationContext* const txn, const RecordId newHead) {
        return this->pimpl()->setHead(txn, newHead);
    }

    void setIsReady(const bool newIsReady) {
        return this->pimpl()->setIsReady(newIsReady);
    }

    HeadManager* headManager() const {
        return this->pimpl()->headManager();
    }

    // --

    /**
     * Returns true if this index is multikey, and returns false otherwise.
     */
    bool isMultikey() const {
        return this->pimpl()->isMultikey();
    }

    /**
     * Returns the path components that cause this index to be multikey if this index supports
     * path-level multikey tracking, and returns an empty vector if path-level multikey tracking
     * isn't supported.
     *
     * If this index supports path-level multikey tracking but isn't multikey, then this function
     * returns a vector with size equal to the number of elements in the index key pattern where
     * each element in the vector is an empty set.
     */
    MultikeyPaths getMultikeyPaths(OperationContext* const txn) const {
        return this->pimpl()->getMultikeyPaths(txn);
    }

    /**
     * Sets this index to be multikey. Information regarding which newly detected path components
     * cause this index to be multikey can also be specified.
     *
     * If this index doesn't support path-level multikey tracking, then 'multikeyPaths' is ignored.
     *
     * If this index supports path-level multikey tracking, then 'multikeyPaths' must be a vector
     * with size equal to the number of elements in the index key pattern. Additionally, at least
     * one path component of the indexed fields must cause this index to be multikey.
     */
    void setMultikey(OperationContext* const txn, const MultikeyPaths& multikeyPaths) {
        return this->pimpl()->setMultikey(txn, multikeyPaths);
    }

    // if this ready is ready for queries
    bool isReady(OperationContext* const txn) const {
        return this->pimpl()->isReady(txn);
    }

    /**
     * If return value is not boost::none, reads with majority read concern using an older snapshot
     * must treat this index as unfinished.
     */
    boost::optional<SnapshotName> getMinimumVisibleSnapshot() {
        return this->pimpl()->getMinimumVisibleSnapshot();
    }

    void setMinimumVisibleSnapshot(const SnapshotName name) {
        return this->pimpl()->setMinimumVisibleSnapshot(name);
    }
};

class IndexCatalogEntryContainer {
public:
    typedef std::vector<IndexCatalogEntry*>::const_iterator const_iterator;
    typedef std::vector<IndexCatalogEntry*>::const_iterator iterator;

    const_iterator begin() const {
        return _entries.vector().begin();
    }

    const_iterator end() const {
        return _entries.vector().end();
    }

    iterator begin() {
        return _entries.vector().begin();
    }

    iterator end() {
        return _entries.vector().end();
    }

    // TODO: these have to be SUPER SUPER FAST
    // maybe even some pointer trickery is in order
    const IndexCatalogEntry* find(const IndexDescriptor* desc) const;
    IndexCatalogEntry* find(const IndexDescriptor* desc);

    IndexCatalogEntry* find(const std::string& name);


    unsigned size() const {
        return _entries.size();
    }

    // -----------------

    /**
     * Removes from _entries and returns the matching entry or NULL if none matches.
     */
    IndexCatalogEntry* release(const IndexDescriptor* desc);

    bool remove(const IndexDescriptor* desc) {
        IndexCatalogEntry* entry = release(desc);
        delete entry;
        return entry;
    }

    // pass ownership to EntryContainer
    void add(IndexCatalogEntry* entry) {
        _entries.mutableVector().push_back(entry);
    }

private:
    OwnedPointerVector<IndexCatalogEntry> _entries;
};
}  // namespace mongo
