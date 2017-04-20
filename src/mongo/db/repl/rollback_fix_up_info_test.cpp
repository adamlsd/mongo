/**
 *    Copyright 2017 MongoDB Inc.
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

#include "mongo/platform/basic.h"

#include "mongo/bson/simple_bsonobj_comparator.h"
#include "mongo/db/catalog/collection_options.h"
#include "mongo/db/client.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/repl/oplog_entry.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/repl/replication_coordinator_mock.h"
#include "mongo/db/repl/rollback_fix_up_info.h"
#include "mongo/db/repl/storage_interface.h"
#include "mongo/db/repl/storage_interface_impl.h"
#include "mongo/db/service_context_d_test_fixture.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/uuid.h"

namespace {

using namespace mongo;
using namespace mongo::repl;

/**
 * Creates ReplSettings for ReplicationCoordinatorMock.
 */
ReplSettings createReplSettings() {
    ReplSettings settings;
    settings.setOplogSizeBytes(5 * 1024 * 1024);
    settings.setReplSetString("mySet/node1:12345");
    return settings;
}

/**
 * Creates an OperationContext using the current Client.
 */
ServiceContext::UniqueOperationContext makeOpCtx() {
    return cc().makeOperationContext();
}

class RollbackFixUpInfoTest : public ServiceContextMongoDTest {
private:
    void setUp() override;
    void tearDown() override;

protected:
    /**
     * Check collection contents against given vector of documents.
     * Ordering of documents in collection does not need to match order in provided vector.
     */
    void _assertDocumentsInCollectionEquals(OperationContext* opCtx,
                                            const NamespaceString& nss,
                                            std::initializer_list<BSONObj> expectedDocs);

    std::unique_ptr<StorageInterface> _storageInterface;
};

void RollbackFixUpInfoTest::setUp() {
    ServiceContextMongoDTest::setUp();
    _storageInterface = stdx::make_unique<StorageInterfaceImpl>();
    auto serviceContext = getServiceContext();
    ReplicationCoordinator::set(
        serviceContext,
        stdx::make_unique<ReplicationCoordinatorMock>(serviceContext, createReplSettings()));

    auto opCtx = makeOpCtx();
    ASSERT_OK(_storageInterface->createCollection(
        opCtx.get(), RollbackFixUpInfo::kRollbackDocsNamespace, {}));
    ASSERT_OK(_storageInterface->createCollection(
        opCtx.get(), RollbackFixUpInfo::kRollbackCollectionUuidNamespace, {}));
    ASSERT_OK(_storageInterface->createCollection(
        opCtx.get(), RollbackFixUpInfo::kRollbackCollectionOptionsNamespace, {}));
    ASSERT_OK(_storageInterface->createCollection(
        opCtx.get(), RollbackFixUpInfo::kRollbackIndexNamespace, {}));
}

void RollbackFixUpInfoTest::tearDown() {
    _storageInterface = {};
    ServiceContextMongoDTest::tearDown();
}

/**
 * Returns string representation of a vector of BSONObj.
 */
template <typename T>
std::string _toString(const T& docs) {
    str::stream ss;
    ss << "[";
    bool first = true;
    for (const auto& doc : docs) {
        if (first) {
            ss << doc;
            first = false;
        } else {
            ss << ", " << doc;
        }
    }
    ss << "]";
    return ss;
}

void RollbackFixUpInfoTest::_assertDocumentsInCollectionEquals(
    OperationContext* opCtx,
    const NamespaceString& nss,
    std::initializer_list<BSONObj> expectedDocs) {
    auto indexName = "_id_"_sd;
    const auto actualDocs = unittest::assertGet(
        _storageInterface->findDocuments(opCtx,
                                         nss,
                                         indexName,
                                         StorageInterface::ScanDirection::kForward,
                                         {},
                                         BoundInclusion::kIncludeStartKeyOnly,
                                         10000U));
    std::string msg = str::stream() << "expected: " << _toString(expectedDocs)
                                    << "; actual: " << _toString(actualDocs);
    ASSERT_EQUALS(expectedDocs.size(), actualDocs.size()) << msg;

    auto unorderedExpectedDocsSet =
        mongo::SimpleBSONObjComparator::kInstance.makeBSONObjUnorderedSet(expectedDocs);
    for (const auto& doc : actualDocs) {
        std::string docMsg = str::stream() << "Unexpected document " << doc << " in collection "
                                           << nss.ns() << ": " << msg;
        ASSERT_TRUE(unorderedExpectedDocsSet.find(doc) != unorderedExpectedDocsSet.end()) << docMsg;
    }
}

TEST_F(RollbackFixUpInfoTest,
       ProcessInsertDocumentOplogEntryInsertsDocumentIntoRollbackDocsCollectionWithInsertOpType) {
    auto operation = BSON("ts" << Timestamp(Seconds(1), 0) << "h" << 1LL << "op"
                               << "i"
                               << "ns"
                               << "test.t"
                               << "ui"
                               << UUID::gen().toBSON().firstElement()
                               << "o"
                               << BSON("_id"
                                       << "mydocid"
                                       << "a"
                                       << 1));

    auto collectionUuid = unittest::assertGet(UUID::parse(operation["ui"]));
    NamespaceString nss(operation["ns"].String());
    auto docId = operation["o"].Obj()["_id"];

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processSingleDocumentOplogEntry(
        opCtx.get(),
        collectionUuid,
        docId,
        RollbackFixUpInfo::SingleDocumentOpType::kInsert,
        nss.db().toString()));

    auto expectedDocument = BSON(
        "_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement() << "documentId"
                                       << docId)
              << "operationType"
              << "insert"
              << "db"
              << "test"
              << "documentToRestore"
              << BSONNULL);
    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackDocsNamespace, {expectedDocument});
}

TEST_F(RollbackFixUpInfoTest,
       ProcessDeleteDocumentOplogEntryInsertsDocumentIntoRollbackDocsCollectionWithDeleteOpType) {
    auto operation = BSON("ts" << Timestamp(Seconds(1), 0) << "h" << 1LL << "op"
                               << "d"
                               << "ns"
                               << "test.t"
                               << "ui"
                               << UUID::gen().toBSON().firstElement()
                               << "o"
                               << BSON("_id"
                                       << "mydocid"));

    auto collectionUuid = unittest::assertGet(UUID::parse(operation["ui"]));
    NamespaceString nss(operation["ns"].String());
    auto docId = operation["o"].Obj()["_id"];

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processSingleDocumentOplogEntry(
        opCtx.get(),
        collectionUuid,
        docId,
        RollbackFixUpInfo::SingleDocumentOpType::kDelete,
        nss.db().toString()));

    auto expectedDocument = BSON(
        "_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement() << "documentId"
                                       << docId)
              << "operationType"
              << "delete"
              << "db"
              << "test"
              << "documentToRestore"
              << BSONNULL);

    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackDocsNamespace, {expectedDocument});
}

TEST_F(RollbackFixUpInfoTest,
       ProcessUpdateDocumentOplogEntryInsertsDocumentIntoRollbackDocsCollectionWithUpdateOpType) {
    auto operation = BSON("ts" << Timestamp(Seconds(1), 0) << "h" << 1LL << "op"
                               << "d"
                               << "ns"
                               << "test.t"
                               << "ui"
                               << UUID::gen().toBSON().firstElement()
                               << "o2"
                               << BSON("_id"
                                       << "mydocid")
                               << "o"
                               << BSON("$set" << BSON("x" << 2)));

    auto collectionUuid = unittest::assertGet(UUID::parse(operation["ui"]));
    NamespaceString nss(operation["ns"].String());
    auto docId = operation["o2"].Obj()["_id"];

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processSingleDocumentOplogEntry(
        opCtx.get(),
        collectionUuid,
        docId,
        RollbackFixUpInfo::SingleDocumentOpType::kUpdate,
        nss.db().toString()));

    auto expectedDocument = BSON(
        "_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement() << "documentId"
                                       << docId)
              << "operationType"
              << "update"
              << "db"
              << "test"
              << "documentToRestore"
              << BSONNULL);

    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackDocsNamespace, {expectedDocument});
}

TEST_F(
    RollbackFixUpInfoTest,
    ProcessCreateCollectionOplogEntryInsertsDocumentIntoRollbackCollectionUuidCollectionWithEmptyNamespace) {
    auto operation = BSON("ts" << Timestamp(Seconds(1), 0) << "h" << 1LL << "op"
                               << "c"
                               << "ns"
                               << "mydb.$cmd"
                               << "ui"
                               << UUID::gen().toBSON().firstElement()
                               << "o"
                               << BSON("create"
                                       << "mynewcoll"
                                       << "idIndex"
                                       << BSON("v" << 2 << "key" << BSON("_id" << 1) << "name"
                                                   << "_id_"
                                                   << "ns"
                                                   << "mydb.mynewcoll")));
    auto collectionUuid = unittest::assertGet(UUID::parse(operation["ui"]));
    NamespaceString commandNss(operation["ns"].String());
    auto collectionName = operation["o"].Obj().firstElement().String();

    ASSERT_TRUE(OplogEntry(operation).isCommand());

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processCreateCollectionOplogEntry(opCtx.get(), collectionUuid));

    auto expectedDocument = BSON("_id" << collectionUuid.toBSON().firstElement() << "ns"
                                       << "");

    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackCollectionUuidNamespace, {expectedDocument});
}

TEST_F(RollbackFixUpInfoTest,
       ProcessDropCollectionOplogEntryInsertsDocumentIntoRollbackCollectionUuidCollection) {
    auto operation = BSON("ts" << Timestamp(Seconds(1), 0) << "h" << 1LL << "op"
                               << "c"
                               << "ns"
                               << "mydb.$cmd"
                               << "ui"
                               << UUID::gen().toBSON().firstElement()
                               << "o"
                               << BSON("drop"
                                       << "mydroppedcoll"));
    auto collectionUuid = unittest::assertGet(UUID::parse(operation["ui"]));
    NamespaceString commandNss(operation["ns"].String());
    auto collectionName = operation["o"].Obj().firstElement().String();
    NamespaceString nss(commandNss.db(), collectionName);

    ASSERT_TRUE(OplogEntry(operation).isCommand());

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processDropCollectionOplogEntry(opCtx.get(), collectionUuid, nss));

    auto expectedDocument =
        BSON("_id" << collectionUuid.toBSON().firstElement() << "ns" << nss.ns());

    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackCollectionUuidNamespace, {expectedDocument});
}

TEST_F(
    RollbackFixUpInfoTest,
    ProcessRenameCollectionOplogEntryWithDropTargetFalseInsertsOneDocumentIntoRollbackCollectionUuidCollection) {
    auto operation = BSON("ts" << Timestamp(Seconds(1), 0) << "h" << 1LL << "op"
                               << "c"
                               << "ns"
                               << "mydb.$cmd"
                               << "ui"
                               << UUID::gen().toBSON().firstElement()
                               << "o"
                               << BSON("renameCollection"
                                       << "mydb.prevCollName"
                                       << "to"
                                       << "mydb.newCollName"
                                       << "stayTemp"
                                       << false
                                       << "dropTarget"
                                       << false));
    auto collectionUuid = unittest::assertGet(UUID::parse(operation["ui"]));
    NamespaceString sourceNss(operation["o"].Obj().firstElement().String());
    ASSERT_EQUALS(ErrorCodes::InvalidUUID, UUID::parse(operation["o"].Obj()["dropTarget"]));

    ASSERT_TRUE(OplogEntry(operation).isCommand());

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processRenameCollectionOplogEntry(
        opCtx.get(), collectionUuid, sourceNss, boost::none));

    auto expectedDocument =
        BSON("_id" << collectionUuid.toBSON().firstElement() << "ns" << sourceNss.ns());

    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackCollectionUuidNamespace, {expectedDocument});
}

TEST_F(
    RollbackFixUpInfoTest,
    ProcessRenameCollectionOplogEntryWithValidDropTargetUuidInsertsTwoDocumentsIntoRollbackCollectionUuidCollection) {
    auto operation = BSON("ts" << Timestamp(Seconds(1), 0) << "h" << 1LL << "op"
                               << "c"
                               << "ns"
                               << "mydb.$cmd"
                               << "ui"
                               << UUID::gen().toBSON().firstElement()
                               << "o"
                               << BSON("renameCollection"
                                       << "mydb.prevCollName"
                                       << "to"
                                       << "mydb.newCollName"
                                       << "stayTemp"
                                       << false
                                       << "dropTarget"
                                       << UUID::gen().toBSON().firstElement()));
    auto collectionUuid = unittest::assertGet(UUID::parse(operation["ui"]));
    NamespaceString sourceNss(operation["o"].Obj().firstElement().String());
    NamespaceString targetNss(operation["o"].Obj()["to"].String());
    auto droppedCollectionUuid =
        unittest::assertGet(UUID::parse(operation["o"].Obj()["dropTarget"]));

    ASSERT_TRUE(OplogEntry(operation).isCommand());

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processRenameCollectionOplogEntry(
        opCtx.get(), collectionUuid, sourceNss, std::make_pair(droppedCollectionUuid, targetNss)));

    auto expectedDocument1 =
        BSON("_id" << collectionUuid.toBSON().firstElement() << "ns" << sourceNss.ns());
    auto expectedDocument2 =
        BSON("_id" << droppedCollectionUuid.toBSON().firstElement() << "ns" << targetNss.ns());

    _assertDocumentsInCollectionEquals(opCtx.get(),
                                       RollbackFixUpInfo::kRollbackCollectionUuidNamespace,
                                       {expectedDocument1, expectedDocument2});
}

TEST_F(RollbackFixUpInfoTest,
       ProcessCollModOplogEntryInsertsDocumentIntoRollbackCollectionOptionsCollection) {
    auto operation =
        BSON("ts" << Timestamp(Seconds(1), 0) << "h" << 1LL << "op"
                  << "c"
                  << "ns"
                  << "mydb.$cmd"
                  << "ui"
                  << UUID::gen().toBSON().firstElement()
                  << "o"
                  << BSON("collMod"
                          << "mycoll"
                          << "validator"
                          << BSON("y" << BSON("$exists" << true)))
                  << "o2"
                  << BSON("validator" << BSON("x" << BSON("$exists" << true)) << "validationLevel"
                                      << "strict"
                                      << "validationAction"
                                      << "error"));
    auto collectionUuid = unittest::assertGet(UUID::parse(operation["ui"]));
    auto optionsObj = operation["o2"].Obj();

    ASSERT_TRUE(OplogEntry(operation).isCommand());

    CollectionOptions options;
    ASSERT_OK(options.parse(optionsObj));
    ASSERT_OK(options.validate());

    auto opCtx = makeOpCtx();

    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processCollModOplogEntry(opCtx.get(), collectionUuid, optionsObj));

    auto expectedDocument =
        BSON("_id" << collectionUuid.toBSON().firstElement() << "options" << optionsObj);

    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackCollectionOptionsNamespace, {expectedDocument});
}

TEST_F(RollbackFixUpInfoTest,
       ProcessCreateIndexOplogEntryInsertsDocumentIntoRollbackIndexCollectionWithEmptyInfoObj) {
    auto operation =
        BSON("ts" << Timestamp(Seconds(1), 0) << "h" << 1LL << "op"
                  << "c"
                  << "ns"
                  << "mydb.$cmd"
                  << "ui"
                  << UUID::gen().toBSON().firstElement()
                  << "o"
                  << BSON("createIndex" << 1 << "v" << 2 << "key" << BSON("b" << 1) << "name"
                                        << "b_1"
                                        << "ns"
                                        << "mydb.mycoll"
                                        << "expireAfterSeconds"
                                        << 60));
    auto collectionUuid = unittest::assertGet(UUID::parse(operation["ui"]));
    auto indexName = operation["o"].Obj()["name"].String();

    ASSERT_TRUE(OplogEntry(operation).isCommand());

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(
        rollbackFixUpInfo.processCreateIndexOplogEntry(opCtx.get(), collectionUuid, indexName));

    auto expectedDocument =
        BSON("_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement() << "indexName"
                                            << indexName)
                   << "operationType"
                   << "create"
                   << "infoObj"
                   << BSONObj());

    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackIndexNamespace, {expectedDocument});
}

TEST_F(RollbackFixUpInfoTest,
       ProcessCreateIndexOplogEntryWhenExistingDocumentHasDropOpTypeRemovesExistingDocument) {

    // State of oplog:
    // {createIndex: indexA}, ...., {dropIndexes: indexA}, ....
    // (earliest optime) ---> (latest optime)
    //
    // Oplog entries are processed in reverse optime order.

    // First, process dropIndexes. This should insert a document into the collection with a 'drop'
    // op type.
    auto collectionUuid = UUID::gen();
    std::string indexName = "b_1";
    auto infoObj = BSON("v" << 2 << "key" << BSON("b" << 1) << "name" << indexName << "ns"
                            << "mydb.mycoll");

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processDropIndexOplogEntry(
        opCtx.get(), collectionUuid, indexName, infoObj));
    _assertDocumentsInCollectionEquals(
        opCtx.get(),
        RollbackFixUpInfo::kRollbackIndexNamespace,
        {BSON("_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement()
                                             << "indexName"
                                             << indexName)
                    << "operationType"
                    << "drop"
                    << "infoObj"
                    << infoObj)});

    // Next, process createIndex. This should cancel out the existing 'drop' operation and remove
    // existing document from the collection.
    ASSERT_OK(
        rollbackFixUpInfo.processCreateIndexOplogEntry(opCtx.get(), collectionUuid, indexName));
    _assertDocumentsInCollectionEquals(opCtx.get(), RollbackFixUpInfo::kRollbackIndexNamespace, {});
}

TEST_F(RollbackFixUpInfoTest,
       ProcessCreateIndexOplogEntryWhenExistingDocumentHasUpdateTTLOpTypeReplacesExistingDocument) {

    // State of oplog:
    // {createIndex: indexA}, ...., {collMod: indexA}, ....
    // (earliest optime) ---> (latest optime)
    //
    // Oplog entries are processed in reverse optime order.

    // First, process collMod. This should insert a document into the collection with an 'updateTTL'
    // op type.
    auto collectionUuid = UUID::gen();
    std::string indexName = "b_1";

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processUpdateIndexTTLOplogEntry(
        opCtx.get(), collectionUuid, indexName, Seconds(60)));
    _assertDocumentsInCollectionEquals(
        opCtx.get(),
        RollbackFixUpInfo::kRollbackIndexNamespace,
        {BSON("_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement()
                                             << "indexName"
                                             << indexName)
                    << "operationType"
                    << "updateTTL"
                    << "infoObj"
                    << BSON("expireAfterSeconds" << 60))});

    // Next, process createIndex. This should replace the existing 'updateTTL' operation so that
    // we drop the index when it's time to apply the fix up info.
    ASSERT_OK(
        rollbackFixUpInfo.processCreateIndexOplogEntry(opCtx.get(), collectionUuid, indexName));
    _assertDocumentsInCollectionEquals(
        opCtx.get(),
        RollbackFixUpInfo::kRollbackIndexNamespace,
        {BSON("_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement()
                                             << "indexName"
                                             << indexName)
                    << "operationType"
                    << "create"
                    << "infoObj"
                    << BSONObj())});
}

TEST_F(
    RollbackFixUpInfoTest,
    ProcessCreateIndexOplogEntryReplacesExistingDocumentAndReturnsFailedToParseErrorWhenExistingDocumentContainsUnrecognizedOperationType) {

    auto collectionUuid = UUID::gen();
    std::string indexName = "b_1";

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());

    auto malformedDoc =
        BSON("_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement() << "indexName"
                                            << indexName)
                   << "operationType"
                   << "unknownIndexOpType"
                   << "infoObj"
                   << BSON("expireAfterSeconds" << 60));
    ASSERT_OK(_storageInterface->upsertById(opCtx.get(),
                                            RollbackFixUpInfo::kRollbackIndexNamespace,
                                            malformedDoc["_id"],
                                            malformedDoc));
    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackIndexNamespace, {malformedDoc});

    // Process createIndex. This should log an error when checking the operation type on the
    // existing document. The malformed document should be replaced.
    ASSERT_OK(
        rollbackFixUpInfo.processCreateIndexOplogEntry(opCtx.get(), collectionUuid, indexName));

    auto expectedDocument =
        BSON("_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement() << "indexName"
                                            << indexName)
                   << "operationType"
                   << "create"
                   << "infoObj"
                   << BSONObj());

    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackIndexNamespace, {expectedDocument});
}

TEST_F(
    RollbackFixUpInfoTest,
    ProcessUpdateIndexTTLOplogEntryInsertsDocumentIntoRollbackIndexCollectionWithPartialInfoObj) {
    auto operation = BSON("ts" << Timestamp(Seconds(1), 0) << "h" << 1LL << "op"
                               << "c"
                               << "ns"
                               << "mydb.$cmd"
                               << "ui"
                               << UUID::gen().toBSON().firstElement()
                               << "o"
                               << BSON("collMod"
                                       << "mycoll"
                                       << "index"
                                       << BSON("name"
                                               << "b_1"
                                               << "expireAfterSeconds"
                                               << 120))
                               << "o2"
                               << BSON("expireAfterSeconds_before" << 60));
    auto collectionUuid = unittest::assertGet(UUID::parse(operation["ui"]));
    auto indexName = operation["o"].Obj().firstElement().String();
    auto expireAfterSeconds =
        mongo::Seconds(operation["o2"].Obj()["expireAfterSeconds_before"].numberLong());
    auto infoObj = BSON("expireAfterSeconds" << durationCount<Seconds>(expireAfterSeconds));

    ASSERT_TRUE(OplogEntry(operation).isCommand());

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processUpdateIndexTTLOplogEntry(
        opCtx.get(), collectionUuid, indexName, expireAfterSeconds));

    auto expectedDocument =
        BSON("_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement() << "indexName"
                                            << indexName)
                   << "operationType"
                   << "updateTTL"
                   << "infoObj"
                   << infoObj);

    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackIndexNamespace, {expectedDocument});
}

TEST_F(
    RollbackFixUpInfoTest,
    ProcessUpdateIndexTTLOplogEntryWhenExistingDocumentHasDropOpTypeUpdatesExpirationInExistingDocument) {
    auto collectionUuid = UUID::gen();
    NamespaceString nss("mydb.mycoll");
    std::string indexName = "b_1";

    // First populate collection with document with optype 'drop' and an indexinfo obj
    // describing a TTL index with a expiration of 120 seconds.
    // This document is the result of processing a dropIndexes oplog entry as we start rollback.
    auto infoObj =
        BSON("v" << 2 << "key" << BSON("b" << 1) << "name" << indexName << "ns" << nss.ns()
                 << "expireAfterSeconds"
                 << 120);

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processDropIndexOplogEntry(
        opCtx.get(), collectionUuid, indexName, infoObj));

    // Process a collMod oplog entry that changes the expiration from 60 seconds to 120 seconds.
    // Chronologically, this operation happens before the dropIndexes command but since oplog
    // entries are processed in reverse order, we process the collMod operation after dropIndexes.
    // We provide the previous 'expireAfterSeconds' value (60 seconds) to
    // processUpdateTTLOplogEntry().
    ASSERT_OK(rollbackFixUpInfo.processUpdateIndexTTLOplogEntry(
        opCtx.get(), collectionUuid, indexName, Seconds(60)));

    // Expected index info obj is the same as 'infoObj' except for the 'expireAfterSeconds' field
    // which should reflect the TTL expiration passed to processUpdateIndexTTLOplogEntry().
    BSONObjBuilder bob;
    for (const auto& elt : infoObj) {
        if ("expireAfterSeconds"_sd == elt.fieldNameStringData()) {
            bob.append("expireAfterSeconds", 60);
        } else {
            bob.append(elt);
        }
    }
    auto expectedInfoObj = bob.obj();

    auto expectedDocument =
        BSON("_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement() << "indexName"
                                            << indexName)
                   << "operationType"
                   << "drop"
                   << "infoObj"
                   << expectedInfoObj);

    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackIndexNamespace, {expectedDocument});
}

TEST_F(
    RollbackFixUpInfoTest,
    ProcessUpdateIndexTTLOplogEntryWhenExistingDocumentHasUpdateTTLOpTypeReplacesExistingDocument) {
    auto collectionUuid = UUID::gen();
    std::string indexName = "b_1";

    // First, process a collMod oplog entry to populate the collection with document with optype
    // 'updateTTL' and an expiration of 120 seconds. 120 seconds is the expiration of the TTL index
    // BEFORE the oplog entry was applied and is what goes into the rollback fix up info.
    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processUpdateIndexTTLOplogEntry(
        opCtx.get(), collectionUuid, indexName, Seconds(120)));

    // Process a second collMod oplog entry that changes the expiration from 60 seconds to 120
    // seconds.
    // This should simply update the expiration in the existing "updateTTL" document in the
    // "kRollbackIndexNamespace" collection. We provide the previous 'expireAfterSeconds' value
    // (60 seconds) to processUpdateTTLOplogEntry().
    ASSERT_OK(rollbackFixUpInfo.processUpdateIndexTTLOplogEntry(
        opCtx.get(), collectionUuid, indexName, Seconds(60)));

    auto expectedDocument =
        BSON("_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement() << "indexName"
                                            << indexName)
                   << "operationType"
                   << "updateTTL"
                   << "infoObj"
                   << BSON("expireAfterSeconds" << 60));

    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackIndexNamespace, {expectedDocument});
}

TEST_F(RollbackFixUpInfoTest,
       ProcessDropIndexOplogEntryInsertsDocumentIntoRollbackIndexCollectionWithCompleteInfoObj) {
    auto operation = BSON("ts" << Timestamp(Seconds(1), 0) << "h" << 1LL << "op"
                               << "c"
                               << "ns"
                               << "mydb.$cmd"
                               << "ui"
                               << UUID::gen().toBSON().firstElement()
                               << "o"
                               << BSON("dropIndexes"
                                       << "mycoll"
                                       << "index"
                                       << "b_1")
                               << "o2"
                               << BSON("v" << 2 << "key" << BSON("b" << 1) << "name"
                                           << "b_1"
                                           << "ns"
                                           << "mydb.mycoll"
                                           << "expireAfterSeconds"
                                           << 120));
    auto collectionUuid = unittest::assertGet(UUID::parse(operation["ui"]));
    auto indexName = operation["o"].Obj()["index"].String();
    auto infoObj = operation["o2"].Obj();

    ASSERT_TRUE(OplogEntry(operation).isCommand());

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(rollbackFixUpInfo.processDropIndexOplogEntry(
        opCtx.get(), collectionUuid, indexName, infoObj));

    auto expectedDocument =
        BSON("_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement() << "indexName"
                                            << indexName)
                   << "operationType"
                   << "drop"
                   << "infoObj"
                   << infoObj);

    _assertDocumentsInCollectionEquals(
        opCtx.get(), RollbackFixUpInfo::kRollbackIndexNamespace, {expectedDocument});
}

TEST_F(RollbackFixUpInfoTest,
       ProcessDropIndexOplogEntryWhenExistingDocumentHasCreateOpTypeReplacesExistingDocument) {

    // State of oplog:
    // {dropIndexes: indexA}, ...., {createIndex: indexA}, ....
    // (earliest optime) ---> (latest optime)
    //
    // Oplog entries are processed in reverse optime order.

    // First, process createIndex. This should insert a document into the collection with a 'create'
    // op type.
    auto collectionUuid = UUID::gen();
    std::string indexName = "b_1";
    auto infoObj = BSON("v" << 2 << "key" << BSON("b" << 1) << "name" << indexName << "ns"
                            << "mydb.mycoll");

    auto opCtx = makeOpCtx();
    RollbackFixUpInfo rollbackFixUpInfo(_storageInterface.get());
    ASSERT_OK(
        rollbackFixUpInfo.processCreateIndexOplogEntry(opCtx.get(), collectionUuid, indexName));
    _assertDocumentsInCollectionEquals(
        opCtx.get(),
        RollbackFixUpInfo::kRollbackIndexNamespace,
        {BSON("_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement()
                                             << "indexName"
                                             << indexName)
                    << "operationType"
                    << "create"
                    << "infoObj"
                    << BSONObj())});

    // Next, process dropIndexes. This should replace the existing 'create' operation with an entry
    // with the 'drop' operation type. When fixing up the indexes for the 'drop' (ie. we need to
    // re-create the index), we would have to drop any existing indexes in the collection with the
    // same name before proceeding with the index creation
    ASSERT_OK(rollbackFixUpInfo.processDropIndexOplogEntry(
        opCtx.get(), collectionUuid, indexName, infoObj));
    _assertDocumentsInCollectionEquals(
        opCtx.get(),
        RollbackFixUpInfo::kRollbackIndexNamespace,
        {BSON("_id" << BSON("collectionUuid" << collectionUuid.toBSON().firstElement()
                                             << "indexName"
                                             << indexName)
                    << "operationType"
                    << "drop"
                    << "infoObj"
                    << infoObj)});
}

}  // namespace
