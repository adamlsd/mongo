// index_catalog.h

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

#include <vector>

#include "mongo/db/catalog/index_catalog_entry.h"
#include "mongo/db/index/multikey_paths.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/record_id.h"
#include "mongo/db/server_options.h"
#include "mongo/db/storage/record_store.h"
#include "mongo/platform/unordered_map.h"
#include "mongo/stdx/functional.h"

namespace mongo {
class Client;
class Collection;

class IndexDescriptor;
class IndexAccessMethod;
struct InsertDeleteOptions;

class IndexCatalogImpl;

/**
 * how many: 1 per Collection.
 * lifecycle: attached to a Collection.
 */
class IndexCatalog {
public:
    class IndexIterator {
    public:
        class Impl {
        public:
            virtual ~Impl();

        private:
            virtual Impl* clone_impl() const = 0;

        public:
            std::unique_ptr<Impl> clone() const {
                return std::unique_ptr<Impl>(this->clone_impl());
            }

            virtual bool more() = 0;

            virtual IndexDescriptor* next() = 0;

            virtual IndexAccessMethod* accessMethod(const IndexDescriptor* desc) = 0;

            virtual IndexCatalogEntry* catalogEntry(const IndexDescriptor* desc) = 0;
        };

    private:
        std::unique_ptr<Impl> _pimpl;

        Impl* pimpl();

        const Impl* pimpl() const;

    public:
        ~IndexIterator() = default;

        explicit inline IndexIterator(std::unique_ptr<IndexIterator::Impl> impl)
            : _pimpl(std::move(impl)) {}

        inline IndexIterator(const IndexIterator& copy) : _pimpl(copy.pimpl()->clone()) {}
        inline IndexIterator(IndexIterator&& copy) = default;

        inline IndexIterator& operator=(IndexIterator copy) {
            using std::swap;
            swap(this->_pimpl, copy._pimpl);

            return *this;
        }

        inline bool more() {
            return this->pimpl()->more();
        }

        inline IndexDescriptor* next() {
            return this->pimpl()->next();
        }

        inline IndexAccessMethod* accessMethod(const IndexDescriptor* const desc) {
            return this->pimpl()->accessMethod(desc);
        }

        inline IndexCatalogEntry* catalogEntry(const IndexDescriptor* const desc) {
            return this->pimpl()->catalogEntry(desc);
        }
    };

    /**
     * Disk creation order.
     * 1) system.indexes entry
     * 2) collection's NamespaceDetails
     *    a) info + head
     *    b) _indexBuildsInProgress++
     * 3) indexes entry in .ns file
     * 4) system.namespaces entry for index ns
     * --- this probably becomes private?
     */
    class IndexBuildBlock {
    public:
        class Impl {
        public:
            virtual ~Impl();

            virtual Status init() = 0;

            virtual void success() = 0;

            virtual void fail() = 0;

            virtual IndexCatalogEntry* getEntry() = 0;
        };

    private:
        std::unique_ptr<Impl> _pimpl;

        Impl* pimpl();

        const Impl* pimpl() const;

        static std::unique_ptr<Impl> makeImpl(OperationContext* txn,
                                              Collection* collection,
                                              const BSONObj& spec);

    public:
        static void registerFactory(stdx::function<std::unique_ptr<Impl>(
                                        OperationContext*, Collection*, const BSONObj&)> factory);

        inline ~IndexBuildBlock() = default;

        explicit inline IndexBuildBlock(OperationContext* const txn,
                                        Collection* const collection,
                                        const BSONObj& spec)
            : _pimpl(makeImpl(txn, collection, spec)) {}

        inline Status init() {
            return this->pimpl()->init();
        }

        inline void success() {
            return this->pimpl()->success();
        }

        /**
         * Index build failed, clean up meta data.
         */
        inline void fail() {
            return this->pimpl()->fail();
        }

        inline IndexCatalogEntry* getEntry() {
            return this->pimpl()->getEntry();
        }
    };

    class Impl {
    public:
        virtual ~Impl();

        virtual Status init(OperationContext* txn) = 0;

        virtual bool ok() const = 0;

        virtual bool haveAnyIndexes() const = 0;

        virtual int numIndexesTotal(OperationContext* txn) const = 0;

        virtual int numIndexesReady(OperationContext* txn) const = 0;

        virtual bool haveIdIndex(OperationContext* txn) const = 0;

        virtual BSONObj getDefaultIdIndexSpec(ServerGlobalParams::FeatureCompatibility::Version
                                                  featureCompatibilityVersion) const = 0;

        virtual IndexDescriptor* findIdIndex(OperationContext* txn) const = 0;

        virtual IndexDescriptor* findIndexByName(OperationContext* txn,
                                                 StringData name,
                                                 bool includeUnfinishedIndexes) const = 0;

        virtual IndexDescriptor* findIndexByKeyPatternAndCollationSpec(
            OperationContext* txn,
            const BSONObj& key,
            const BSONObj& collationSpec,
            bool includeUnfinishedIndexes) const = 0;

        virtual void findIndexesByKeyPattern(OperationContext* txn,
                                             const BSONObj& key,
                                             bool includeUnfinishedIndexes,
                                             std::vector<IndexDescriptor*>* matches) const = 0;

        virtual IndexDescriptor* findShardKeyPrefixedIndex(OperationContext* txn,
                                                           const BSONObj& shardKey,
                                                           bool requireSingleKey) const = 0;

        virtual void findIndexByType(OperationContext* txn,
                                     const std::string& type,
                                     std::vector<IndexDescriptor*>& matches,
                                     bool includeUnfinishedIndexes) const = 0;

        virtual const IndexDescriptor* refreshEntry(OperationContext* txn,
                                                    const IndexDescriptor* oldDesc) = 0;

        virtual const IndexCatalogEntry* getEntry(const IndexDescriptor* desc) const = 0;

        virtual IndexAccessMethod* getIndex(const IndexDescriptor* desc) = 0;

        virtual const IndexAccessMethod* getIndex(const IndexDescriptor* desc) const = 0;

        virtual Status checkUnfinished() const = 0;

        virtual IndexIterator getIndexIterator(OperationContext* txn,
                                               bool includeUnfinishedIndexes) const = 0;

        virtual StatusWith<BSONObj> createIndexOnEmptyCollection(OperationContext* txn,
                                                                 BSONObj spec) = 0;

        virtual StatusWith<BSONObj> prepareSpecForCreate(OperationContext* txn,
                                                         const BSONObj& original) const = 0;

        virtual Status dropAllIndexes(OperationContext* txn, bool includingIdIndex) = 0;

        virtual Status dropIndex(OperationContext* txn, IndexDescriptor* desc) = 0;

        virtual std::vector<BSONObj> getAndClearUnfinishedIndexes(OperationContext* txn) = 0;

        virtual bool isMultikey(OperationContext* txn, const IndexDescriptor* idx) = 0;

        virtual MultikeyPaths getMultikeyPaths(OperationContext* txn,
                                               const IndexDescriptor* idx) = 0;

        virtual Status indexRecords(OperationContext* txn,
                                    const std::vector<BsonRecord>& bsonRecords,
                                    int64_t* keysInsertedOut) = 0;

        virtual void unindexRecord(OperationContext* txn,
                                   const BSONObj& obj,
                                   const RecordId& loc,
                                   bool noWarn,
                                   int64_t* keysDeletedOut) = 0;

        virtual std::string getAccessMethodName(OperationContext* txn,
                                                const BSONObj& keyPattern) = 0;

        virtual Status upgradeDatabaseMinorVersionIfNeeded(OperationContext* txn,
                                                           const std::string& newPluginName) = 0;


        // Pseudo-private accessors and methods:

        virtual const Collection* collection() const = 0;
        virtual Collection* collection() = 0;

        virtual const IndexCatalogEntryContainer& entries() const = 0;
        virtual IndexCatalogEntryContainer& entries() = 0;

        virtual IndexCatalogEntry* _setupInMemoryStructures(OperationContext* txn,
                                                            IndexDescriptor* descriptor,
                                                            bool initFromDisk) = 0;

        virtual Status _dropIndex(OperationContext* txn, IndexCatalogEntry* desc) = 0;
        virtual void _deleteIndexFromDisk(OperationContext* const txn,
                                          const std::string& indexName,
                                          const std::string& indexNamespace) = 0;
    };

private:
    std::unique_ptr<Impl> _pimpl;

    Impl* pimpl();

    const Impl* pimpl() const;

    static std::unique_ptr<Impl> makeImpl(Collection*);

public:
    static void registerFactory(stdx::function<std::unique_ptr<Impl>(Collection*)> factory);

    inline IndexCatalog(Collection* const collection) : _pimpl(makeImpl(collection)) {}

    inline ~IndexCatalog() = default;

    // must be called before used
    inline Status init(OperationContext* const txn) {
        return this->pimpl()->init(txn);
    }

    inline bool ok() const {
        return this->pimpl()->ok();
    }

    // ---- accessors -----

    inline bool haveAnyIndexes() const {
        return this->pimpl()->haveAnyIndexes();
    }

    inline int numIndexesTotal(OperationContext* const txn) const {
        return this->pimpl()->numIndexesTotal(txn);
    }
    inline int numIndexesReady(OperationContext* const txn) const {
        return this->pimpl()->numIndexesReady(txn);
    }

    inline int numIndexesInProgress(OperationContext* txn) const {
        return numIndexesTotal(txn) - numIndexesReady(txn);
    }

    /**
     * This is in "alive" until the Collection goes away
     * in which case everything from this tree has to go away.
     */

    inline bool haveIdIndex(OperationContext* const txn) const {
        return this->pimpl()->haveIdIndex(txn);
    }

    /**
     * Returns the spec for the id index to create by default for this collection.
     */
    inline BSONObj getDefaultIdIndexSpec(
        const ServerGlobalParams::FeatureCompatibility::Version featureCompatibilityVersion) const {
        return this->pimpl()->getDefaultIdIndexSpec(featureCompatibilityVersion);
    }

    inline IndexDescriptor* findIdIndex(OperationContext* const txn) const {
        return this->pimpl()->findIdIndex(txn);
    }

    /**
     * Find index by name.  The index name uniquely identifies an index.
     *
     * @return null if cannot find
     */
    inline IndexDescriptor* findIndexByName(OperationContext* const txn,
                                            const StringData name,
                                            const bool includeUnfinishedIndexes = false) const {
        return this->pimpl()->findIndexByName(txn, name, includeUnfinishedIndexes);
    }

    /**
     * Find index by matching key pattern and collation spec.  The key pattern and collation spec
     * uniquely identify an index.
     *
     * Collation is specified as a normalized collation spec as returned by
     * CollationInterface::getSpec.  An empty object indicates the simple collation.
     *
     * @return null if cannot find index, otherwise the index with a matching key pattern and
     * collation.
     */
    inline IndexDescriptor* findIndexByKeyPatternAndCollationSpec(
        OperationContext* const txn,
        const BSONObj& key,
        const BSONObj& collationSpec,
        const bool includeUnfinishedIndexes = false) const {
        return this->pimpl()->findIndexByKeyPatternAndCollationSpec(
            txn, key, collationSpec, includeUnfinishedIndexes);
    }

    /**
     * Find indexes with a matching key pattern, putting them into the vector 'matches'.  The key
     * pattern alone does not uniquely identify an index.
     *
     * Consider using 'findIndexByName' if expecting to match one index.
     */
    inline void findIndexesByKeyPattern(OperationContext* const txn,
                                        const BSONObj& key,
                                        const bool includeUnfinishedIndexes,
                                        std::vector<IndexDescriptor*>* const matches) const {
        return this->pimpl()->findIndexesByKeyPattern(txn, key, includeUnfinishedIndexes, matches);
    }

    /**
     * Returns an index suitable for shard key range scans.
     *
     * This index:
     * - must be prefixed by 'shardKey', and
     * - must not be a partial index.
     * - must have the simple collation.
     *
     * If the parameter 'requireSingleKey' is true, then this index additionally must not be
     * multi-key.
     *
     * If no such index exists, returns NULL.
     */
    inline IndexDescriptor* findShardKeyPrefixedIndex(OperationContext* const txn,
                                                      const BSONObj& shardKey,
                                                      const bool requireSingleKey) const {
        return this->pimpl()->findShardKeyPrefixedIndex(txn, shardKey, requireSingleKey);
    }

    inline void findIndexByType(OperationContext* const txn,
                                const std::string& type,
                                std::vector<IndexDescriptor*>& matches,
                                const bool includeUnfinishedIndexes = false) const {
        return this->pimpl()->findIndexByType(txn, type, matches, includeUnfinishedIndexes);
    }

    /**
     * Reload the index definition for 'oldDesc' from the CollectionCatalogEntry.  'oldDesc'
     * must be a ready index that is already registered with the index catalog.  Returns an
     * unowned pointer to the descriptor for the new index definition.
     *
     * Use this method to notify the IndexCatalog that the spec for this index has changed.
     *
     * It is invalid to dereference 'oldDesc' after calling this method.  This method broadcasts
     * an invalidateAll() on the cursor manager to notify other users of the IndexCatalog that
     * this descriptor is now invalid.
     */
    inline const IndexDescriptor* refreshEntry(OperationContext* const txn,
                                               const IndexDescriptor* const oldDesc) {
        return this->pimpl()->refreshEntry(txn, oldDesc);
    }

    // never returns NULL
    inline const IndexCatalogEntry* getEntry(const IndexDescriptor* const desc) const {
        return this->pimpl()->getEntry(desc);
    }

    inline IndexAccessMethod* getIndex(const IndexDescriptor* const desc) {
        return this->pimpl()->getIndex(desc);
    }

    inline const IndexAccessMethod* getIndex(const IndexDescriptor* const desc) const {
        return this->pimpl()->getIndex(desc);
    }

    /**
     * Returns a not-ok Status if there are any unfinished index builds. No new indexes should
     * be built when in this state.
     */
    inline Status checkUnfinished() const {
        return this->pimpl()->checkUnfinished();
    }

    inline IndexIterator getIndexIterator(OperationContext* const txn,
                                          const bool includeUnfinishedIndexes) const {
        return this->pimpl()->getIndexIterator(txn, includeUnfinishedIndexes);
    };

    // ---- index set modifiers ------

    /**
     * Call this only on an empty collection from inside a WriteUnitOfWork. Index creation on an
     * empty collection can be rolled back as part of a larger WUOW. Returns the full specification
     * of the created index, as it is stored in this index catalog.
     */
    inline StatusWith<BSONObj> createIndexOnEmptyCollection(OperationContext* const txn,
                                                            BSONObj spec) {
        return this->pimpl()->createIndexOnEmptyCollection(txn, std::move(spec));
    }

    inline StatusWith<BSONObj> prepareSpecForCreate(OperationContext* const txn,
                                                    const BSONObj& original) const {
        return this->pimpl()->prepareSpecForCreate(txn, original);
    }

    inline Status dropAllIndexes(OperationContext* const txn, const bool includingIdIndex) {
        return this->pimpl()->dropAllIndexes(txn, includingIdIndex);
    }

    inline Status dropIndex(OperationContext* const txn, IndexDescriptor* const desc) {
        return this->pimpl()->dropIndex(txn, desc);
    }

    /**
     * Will drop all incompleted indexes and return specs
     * after this, the indexes can be rebuilt.
     */
    inline std::vector<BSONObj> getAndClearUnfinishedIndexes(OperationContext* const txn) {
        return this->pimpl()->getAndClearUnfinishedIndexes(txn);
    }


    struct IndexKillCriteria {
        std::string ns;
        std::string name;
        BSONObj key;
    };

    // ---- modify single index

    /**
     * Returns true if the index 'idx' is multikey, and returns false otherwise.
     */
    inline bool isMultikey(OperationContext* const txn, const IndexDescriptor* const idx) {
        return this->pimpl()->isMultikey(txn, idx);
    }

    /**
     * Returns the path components that cause the index 'idx' to be multikey if the index supports
     * path-level multikey tracking, and returns an empty vector if path-level multikey tracking
     * isn't supported.
     *
     * If the index supports path-level multikey tracking but isn't multikey, then this function
     * returns a vector with size equal to the number of elements in the index key pattern where
     * each element in the vector is an empty set.
     */
    inline MultikeyPaths getMultikeyPaths(OperationContext* const txn,
                                          const IndexDescriptor* const idx) {
        return this->pimpl()->getMultikeyPaths(txn, idx);
    }

    // --- these probably become private?


    // ----- data modifiers ------

    /**
     * When 'keysInsertedOut' is not null, it will be set to the number of index keys inserted by
     * this operation.
     *
     * This method may throw.
     */
    inline Status indexRecords(OperationContext* const txn,
                               const std::vector<BsonRecord>& bsonRecords,
                               int64_t* const keysInsertedOut) {
        return this->pimpl()->indexRecords(txn, bsonRecords, keysInsertedOut);
    }

    /**
     * When 'keysDeletedOut' is not null, it will be set to the number of index keys removed by
     * this operation.
     */
    inline void unindexRecord(OperationContext* const txn,
                              const BSONObj& obj,
                              const RecordId& loc,
                              const bool noWarn,
                              int64_t* const keysDeletedOut) {
        return this->pimpl()->unindexRecord(txn, obj, loc, noWarn, keysDeletedOut);
    }

    // ------- temp internal -------

    inline std::string getAccessMethodName(OperationContext* const txn, const BSONObj& keyPattern) {
        return this->pimpl()->getAccessMethodName(txn, keyPattern);
    }

    inline Status upgradeDatabaseMinorVersionIfNeeded(OperationContext* const txn,
                                                      const std::string& newPluginName) {
        return this->pimpl()->upgradeDatabaseMinorVersionIfNeeded(txn, newPluginName);
    }

private:
    friend IndexCatalogImpl;

    const Collection* collection() const {
        return this->pimpl()->collection();
    }

    Collection* collection() {
        return this->pimpl()->collection();
    }

    inline IndexCatalogEntryContainer& entries() {
        return this->pimpl()->entries();
    }
    inline const IndexCatalogEntryContainer& entries() const {
        return this->pimpl()->entries();
    }

    inline IndexCatalogEntry* setupInMemoryStructures(OperationContext* const txn,
                                                      IndexDescriptor* const descriptor,
                                                      const bool initFromDisk) {
        return this->pimpl()->_setupInMemoryStructures(txn, descriptor, initFromDisk);
    }

    /**
     * this does no sanity checks.
     */
    inline Status _dropIndex(OperationContext* const txn, IndexCatalogEntry* const entry) {
        return this->pimpl()->_dropIndex(txn, entry);
    }

    // just does disk hanges
    // doesn't change memory state, etc...
    inline void _deleteIndexFromDisk(OperationContext* const txn,
                                     const std::string& indexName,
                                     const std::string& indexNamespace) {
        return this->pimpl()->_deleteIndexFromDisk(txn, indexName, indexNamespace);
    }
};
}  // namespace mongo
