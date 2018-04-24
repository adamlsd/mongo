/*    Copyright (C) 2016 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/catalog_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/s/collection_sharding_state.h"
#include "mongo/db/s/shard_server_op_observer.h"
#include "mongo/db/s/sharding_state.h"
#include "mongo/db/s/type_shard_identity.h"
#include "mongo/s/shard_server_test_fixture.h"

namespace mongo {
namespace {

const NamespaceString kTestNss("TestDB", "TestColl");

class CollectionShardingStateTest : public ShardServerTestFixture {
public:
    void setUp() override {
        ShardServerTestFixture::setUp();

        // Note: this assumes that globalInit will always be called on the same thread as the main
        // test thread.
        ShardingState::get(operationContext())
            ->setGlobalInitMethodForTest(
                [this](OperationContext*, const ConnectionString&, StringData) {
                    _initCallCount++;
                    return Status::OK();
                });
    }

    int getInitCallCount() const {
        return _initCallCount;
    }

private:
    int _initCallCount = 0;
};

TEST_F(CollectionShardingStateTest, GlobalInitGetsCalledAfterWriteCommits) {
    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName("a");
    shardIdentity.setClusterId(OID::gen());

    DBDirectClient client(operationContext());
    client.insert("admin.system.version", shardIdentity.toBSON());
    ASSERT_EQ(1, getInitCallCount());
}

TEST_F(CollectionShardingStateTest, GlobalInitDoesntGetCalledIfWriteAborts) {
    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName("a");
    shardIdentity.setClusterId(OID::gen());

    // This part of the test ensures that the collection exists for the AutoGetCollection below to
    // find and also validates that the initializer does not get called for non-sharding documents
    DBDirectClient client(operationContext());
    client.insert("admin.system.version", BSON("_id" << 1));
    ASSERT_EQ(0, getInitCallCount());

    {
        AutoGetCollection autoColl(
            operationContext(), NamespaceString("admin.system.version"), MODE_IX);

        WriteUnitOfWork wuow(operationContext());
        ASSERT_OK(autoColl.getCollection()->insertDocument(
            operationContext(), shardIdentity.toBSON(), {}, false));
        ASSERT_EQ(0, getInitCallCount());
    }

    ASSERT_EQ(0, getInitCallCount());
}

TEST_F(CollectionShardingStateTest, GlobalInitDoesntGetsCalledIfNSIsNotForShardIdentity) {
    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName("a");
    shardIdentity.setClusterId(OID::gen());

    DBDirectClient client(operationContext());
    client.insert("admin.user", shardIdentity.toBSON());
    ASSERT_EQ(0, getInitCallCount());
}

TEST_F(CollectionShardingStateTest, OnInsertOpThrowWithIncompleteShardIdentityDocument) {
    ShardIdentityType shardIdentity;
    shardIdentity.setShardName("a");

    DBDirectClient client(operationContext());
    client.insert("admin.system.version", shardIdentity.toBSON());
    ASSERT(!client.getLastError().empty());
}

/**
 * Constructs a CollectionMetadata suitable for refreshing a CollectionShardingState. The only
 * salient detail is the argument `keyPattern` which, defining the shard key, selects the fields
 * that DeleteState's constructor will extract from its `doc` argument into its member
 * DeleteState::documentKey.
 */
std::unique_ptr<CollectionMetadata> makeAMetadata(BSONObj const& keyPattern) {
    const OID epoch = OID::gen();
    auto range = ChunkRange(BSON("key" << MINKEY), BSON("key" << MAXKEY));
    auto chunk = ChunkType(kTestNss, std::move(range), ChunkVersion(1, 0, epoch), ShardId("other"));
    auto rt = RoutingTableHistory::makeNew(
        kTestNss, UUID::gen(), KeyPattern(keyPattern), nullptr, false, epoch, {std::move(chunk)});
    std::shared_ptr<ChunkManager> cm = std::make_shared<ChunkManager>(rt, Timestamp(100, 0));

    return stdx::make_unique<CollectionMetadata>(std::move(cm), ShardId("this"));
}

using DeleteStateTest = ShardServerTestFixture;

TEST_F(DeleteStateTest, MakeDeleteStateUnsharded) {
    AutoGetCollection autoColl(operationContext(), kTestNss, MODE_IX);
    auto* css = CollectionShardingState::get(operationContext(), kTestNss);

    auto doc = BSON("key3"
                    << "abc"
                    << "key"
                    << 3
                    << "_id"
                    << "hello"
                    << "key2"
                    << true);

    // First, check that an order for deletion from an unsharded collection (where css has not been
    // "refreshed" with chunk metadata) extracts just the "_id" field:
    auto deleteState = ShardObserverDeleteState::make(operationContext(), css, doc);
    ASSERT_BSONOBJ_EQ(deleteState.documentKey,
                      BSON("_id"
                           << "hello"));
    ASSERT_FALSE(deleteState.isMigrating);
}

TEST_F(DeleteStateTest, MakeDeleteStateShardedWithoutIdInShardKey) {
    AutoGetCollection autoColl(operationContext(), kTestNss, MODE_IX);
    auto* css = CollectionShardingState::get(operationContext(), kTestNss);

    // Push a CollectionMetadata with a shard key not including "_id"...
    css->refreshMetadata(operationContext(), makeAMetadata(BSON("key" << 1 << "key3" << 1)));

    // The order of fields in `doc` deliberately does not match the shard key
    auto doc = BSON("key3"
                    << "abc"
                    << "key"
                    << 100
                    << "_id"
                    << "hello"
                    << "key2"
                    << true);

    // Verify the shard key is extracted, in correct order, followed by the "_id" field.
    auto deleteState = ShardObserverDeleteState::make(operationContext(), css, doc);
    ASSERT_BSONOBJ_EQ(deleteState.documentKey,
                      BSON("key" << 100 << "key3"
                                 << "abc"
                                 << "_id"
                                 << "hello"));
    ASSERT_FALSE(deleteState.isMigrating);
}

TEST_F(DeleteStateTest, MakeDeleteStateShardedWithIdInShardKey) {
    AutoGetCollection autoColl(operationContext(), kTestNss, MODE_IX);
    auto* css = CollectionShardingState::get(operationContext(), kTestNss);

    // Push a CollectionMetadata with a shard key that does have "_id" in the middle...
    css->refreshMetadata(operationContext(),
                         makeAMetadata(BSON("key" << 1 << "_id" << 1 << "key2" << 1)));

    // The order of fields in `doc` deliberately does not match the shard key
    auto doc = BSON("key2" << true << "key3"
                           << "abc"
                           << "_id"
                           << "hello"
                           << "key"
                           << 100);

    // Verify the shard key is extracted with "_id" in the right place.
    auto deleteState = ShardObserverDeleteState::make(operationContext(), css, doc);
    ASSERT_BSONOBJ_EQ(deleteState.documentKey,
                      BSON("key" << 100 << "_id"
                                 << "hello"
                                 << "key2"
                                 << true));
    ASSERT_FALSE(deleteState.isMigrating);
}

TEST_F(DeleteStateTest, MakeDeleteStateShardedWithIdHashInShardKey) {
    AutoGetCollection autoColl(operationContext(), kTestNss, MODE_IX);
    auto* css = CollectionShardingState::get(operationContext(), kTestNss);

    // Push a CollectionMetadata with a shard key "_id", hashed.
    auto aMetadata = makeAMetadata(BSON("_id"
                                        << "hashed"));
    css->refreshMetadata(operationContext(), std::move(aMetadata));

    auto doc = BSON("key2" << true << "_id"
                           << "hello"
                           << "key"
                           << 100);

    // Verify the shard key is extracted with "_id" in the right place, not hashed.
    auto deleteState = ShardObserverDeleteState::make(operationContext(), css, doc);
    ASSERT_BSONOBJ_EQ(deleteState.documentKey,
                      BSON("_id"
                           << "hello"));
    ASSERT_FALSE(deleteState.isMigrating);
}

}  // namespace
}  // namespace mongo
