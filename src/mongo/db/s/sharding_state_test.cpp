/**
 *    Copyright (C) 2016 MongoDB, Inc.
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

#include "mongo/client/remote_command_targeter_mock.h"
#include "mongo/db/catalog_raii.h"
#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/op_observer_impl.h"
#include "mongo/db/op_observer_registry.h"
#include "mongo/db/repl/replication_coordinator_mock.h"
#include "mongo/db/s/config_server_op_observer.h"
#include "mongo/db/s/shard_server_catalog_cache_loader.h"
#include "mongo/db/s/shard_server_op_observer.h"
#include "mongo/db/s/sharding_state.h"
#include "mongo/db/s/type_shard_identity.h"
#include "mongo/db/server_options.h"
#include "mongo/s/catalog/dist_lock_manager_mock.h"
#include "mongo/s/catalog/sharding_catalog_client_impl.h"
#include "mongo/s/client/shard_registry.h"
#include "mongo/s/config_server_catalog_cache_loader.h"
#include "mongo/s/shard_server_test_fixture.h"
#include "mongo/s/sharding_mongod_test_fixture.h"

namespace mongo {
namespace {

const std::string kShardName("TestShard");

class ShardingInitializationOpObserverTest : public ShardServerTestFixture {
public:
    void setUp() override {
        ShardServerTestFixture::setUp();

        // NOTE: this assumes that globalInit will always be called on the same thread as the main
        // test thread
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

TEST_F(ShardingInitializationOpObserverTest, GlobalInitGetsCalledAfterWriteCommits) {
    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnectionString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName(kShardName);
    shardIdentity.setClusterId(OID::gen());

    DBDirectClient client(operationContext());
    client.insert("admin.system.version", shardIdentity.toShardIdentityDocument());
    ASSERT_EQ(1, getInitCallCount());
}

TEST_F(ShardingInitializationOpObserverTest, GlobalInitDoesntGetCalledIfWriteAborts) {
    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnectionString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName(kShardName);
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
            operationContext(), shardIdentity.toShardIdentityDocument(), {}));
        ASSERT_EQ(0, getInitCallCount());
    }

    ASSERT_EQ(0, getInitCallCount());
}

TEST_F(ShardingInitializationOpObserverTest, GlobalInitDoesntGetsCalledIfNSIsNotForShardIdentity) {
    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnectionString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName(kShardName);
    shardIdentity.setClusterId(OID::gen());

    DBDirectClient client(operationContext());
    client.insert("admin.user", shardIdentity.toShardIdentityDocument());
    ASSERT_EQ(0, getInitCallCount());
}

TEST_F(ShardingInitializationOpObserverTest, OnInsertOpThrowWithIncompleteShardIdentityDocument) {
    DBDirectClient client(operationContext());
    client.insert("admin.system.version",
                  BSON("_id" << ShardIdentityType::IdName << ShardIdentity::kShardNameFieldName
                             << kShardName));
    ASSERT(!client.getLastError().empty());
}


class ShardingStateTest : public ShardingMongodTestFixture {
protected:
    // Used to write to set up local collections before exercising server logic.
    std::unique_ptr<DBDirectClient> _dbDirectClient;

    void setUp() override {
        serverGlobalParams.clusterRole = ClusterRole::None;
        ShardingMongodTestFixture::setUp();

        // When sharding initialization is triggered, initialize sharding state as a shard server.
        serverGlobalParams.clusterRole = ClusterRole::ShardServer;

        CatalogCacheLoader::set(getServiceContext(),
                                stdx::make_unique<ShardServerCatalogCacheLoader>(
                                    stdx::make_unique<ConfigServerCatalogCacheLoader>()));

        _shardingState.setGlobalInitMethodForTest([&](OperationContext* opCtx,
                                                      const ConnectionString& configConnStr,
                                                      StringData distLockProcessId) {
            auto status = initializeGlobalShardingStateForMongodForTest(configConnStr);
            if (!status.isOK()) {
                return status;
            }

            // Set the ConnectionString return value on the mock targeter so that later calls to the
            // targeter's getConnString() return the appropriate value
            auto configTargeter =
                RemoteCommandTargeterMock::get(shardRegistry()->getConfigShard()->getTargeter());
            configTargeter->setConnectionStringReturnValue(configConnStr);
            configTargeter->setFindHostReturnValue(configConnStr.getServers()[0]);

            return Status::OK();
        });

        _dbDirectClient = stdx::make_unique<DBDirectClient>(operationContext());
    }

    void tearDown() override {
        _dbDirectClient.reset();

        // Restore the defaults before calling tearDown
        storageGlobalParams.readOnly = false;
        serverGlobalParams.overrideShardIdentity = BSONObj();

        CatalogCacheLoader::clearForTests(getServiceContext());

        ShardingMongodTestFixture::tearDown();
    }

    std::unique_ptr<DistLockManager> makeDistLockManager(
        std::unique_ptr<DistLockCatalog> distLockCatalog) override {
        return stdx::make_unique<DistLockManagerMock>(nullptr);
    }

    std::unique_ptr<ShardingCatalogClient> makeShardingCatalogClient(
        std::unique_ptr<DistLockManager> distLockManager) override {
        invariant(distLockManager);
        return stdx::make_unique<ShardingCatalogClientImpl>(std::move(distLockManager));
    }

    ShardingState* shardingState() {
        return &_shardingState;
    }

private:
    ShardingState _shardingState;
};

/**
 * This class emulates the server being started as a standalone node for the scope for which it is
 * used
 */
class ScopedSetStandaloneMode {
public:
    ScopedSetStandaloneMode(ServiceContext* serviceContext) : _serviceContext(serviceContext) {
        serverGlobalParams.clusterRole = ClusterRole::None;
        _serviceContext->setOpObserver(stdx::make_unique<OpObserverRegistry>());
    }

    ~ScopedSetStandaloneMode() {
        serverGlobalParams.clusterRole = ClusterRole::ShardServer;
        auto makeOpObserver = [&] {
            auto opObserver = stdx::make_unique<OpObserverRegistry>();
            opObserver->addObserver(stdx::make_unique<OpObserverImpl>());
            opObserver->addObserver(stdx::make_unique<ConfigServerOpObserver>());
            opObserver->addObserver(stdx::make_unique<ShardServerOpObserver>());
            return opObserver;
        };

        _serviceContext->setOpObserver(makeOpObserver());
    }

private:
    ServiceContext* const _serviceContext;
};

TEST_F(ShardingStateTest, ValidShardIdentitySucceeds) {
    // Must hold a lock to call initializeFromShardIdentity.
    Lock::GlobalWrite lk(operationContext());

    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnectionString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName(kShardName);
    shardIdentity.setClusterId(OID::gen());

    ASSERT_OK(shardingState()->initializeFromShardIdentity(operationContext(), shardIdentity));
    ASSERT_TRUE(shardingState()->enabled());
    ASSERT_EQ(kShardName, shardingState()->shardId());
    ASSERT_EQ("config/a:1,b:2", shardRegistry()->getConfigServerConnectionString().toString());
}

TEST_F(ShardingStateTest, InitWhilePreviouslyInErrorStateWillStayInErrorState) {
    // Must hold a lock to call initializeFromShardIdentity.
    Lock::GlobalWrite lk(operationContext());

    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnectionString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName(kShardName);
    shardIdentity.setClusterId(OID::gen());

    shardingState()->setGlobalInitMethodForTest(
        [](OperationContext* opCtx, const ConnectionString& connStr, StringData distLockProcessId) {
            return Status{ErrorCodes::ShutdownInProgress, "shutting down"};
        });

    {
        auto status =
            shardingState()->initializeFromShardIdentity(operationContext(), shardIdentity);
        ASSERT_EQ(ErrorCodes::ShutdownInProgress, status);
    }

    // ShardingState is now in error state, attempting to call it again will still result in error.

    shardingState()->setGlobalInitMethodForTest(
        [](OperationContext* opCtx, const ConnectionString& connStr, StringData distLockProcessId) {
            return Status::OK();
        });

    {
        auto status =
            shardingState()->initializeFromShardIdentity(operationContext(), shardIdentity);
        ASSERT_EQ(ErrorCodes::ManualInterventionRequired, status);
    }

    ASSERT_FALSE(shardingState()->enabled());
}

TEST_F(ShardingStateTest, InitializeAgainWithMatchingShardIdentitySucceeds) {
    // Must hold a lock to call initializeFromShardIdentity.
    Lock::GlobalWrite lk(operationContext());

    auto clusterID = OID::gen();
    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnectionString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName(kShardName);
    shardIdentity.setClusterId(clusterID);

    ASSERT_OK(shardingState()->initializeFromShardIdentity(operationContext(), shardIdentity));

    ShardIdentityType shardIdentity2;
    shardIdentity2.setConfigsvrConnectionString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity2.setShardName(kShardName);
    shardIdentity2.setClusterId(clusterID);

    shardingState()->setGlobalInitMethodForTest(
        [](OperationContext* opCtx, const ConnectionString& connStr, StringData distLockProcessId) {
            return Status{ErrorCodes::InternalError, "should not reach here"};
        });

    ASSERT_OK(shardingState()->initializeFromShardIdentity(operationContext(), shardIdentity2));

    ASSERT_TRUE(shardingState()->enabled());
    ASSERT_EQ(kShardName, shardingState()->shardId());
    ASSERT_EQ("config/a:1,b:2", shardRegistry()->getConfigServerConnectionString().toString());
}

TEST_F(ShardingStateTest, InitializeAgainWithSameReplSetNameSucceeds) {
    // Must hold a lock to call initializeFromShardIdentity.
    Lock::GlobalWrite lk(operationContext());

    auto clusterID = OID::gen();
    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnectionString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName(kShardName);
    shardIdentity.setClusterId(clusterID);

    ASSERT_OK(shardingState()->initializeFromShardIdentity(operationContext(), shardIdentity));

    ShardIdentityType shardIdentity2;
    shardIdentity2.setConfigsvrConnectionString(
        ConnectionString(ConnectionString::SET, "b:2,c:3", "config"));
    shardIdentity2.setShardName(kShardName);
    shardIdentity2.setClusterId(clusterID);

    shardingState()->setGlobalInitMethodForTest(
        [](OperationContext* opCtx, const ConnectionString& connStr, StringData distLockProcessId) {
            return Status{ErrorCodes::InternalError, "should not reach here"};
        });

    ASSERT_OK(shardingState()->initializeFromShardIdentity(operationContext(), shardIdentity2));

    ASSERT_TRUE(shardingState()->enabled());
    ASSERT_EQ(kShardName, shardingState()->shardId());
    ASSERT_EQ("config/a:1,b:2", shardRegistry()->getConfigServerConnectionString().toString());
}

// The tests below check for different combinations of the compatible startup parameters for
// --shardsvr, --overrideShardIdentity, and queryableBackup (readOnly) mode

// readOnly and --shardsvr

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededReadOnlyAndShardServerAndNoOverrideShardIdentity) {
    storageGlobalParams.readOnly = true;
    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_EQUALS(ErrorCodes::InvalidOptions, swShardingInitialized.getStatus().code());
}

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededReadOnlyAndShardServerAndInvalidOverrideShardIdentity) {
    storageGlobalParams.readOnly = true;
    serverGlobalParams.overrideShardIdentity =
        BSON("_id"
             << "shardIdentity"
             << ShardIdentity::kShardNameFieldName
             << kShardName
             << ShardIdentity::kClusterIdFieldName
             << OID::gen()
             << ShardIdentity::kConfigsvrConnectionStringFieldName
             << "invalid");
    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_EQUALS(ErrorCodes::UnsupportedFormat, swShardingInitialized.getStatus().code());
}

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededReadOnlyAndShardServerAndValidOverrideShardIdentity) {
    storageGlobalParams.readOnly = true;
    serverGlobalParams.clusterRole = ClusterRole::ShardServer;

    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnectionString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName(kShardName);
    shardIdentity.setClusterId(OID::gen());
    ASSERT_OK(shardIdentity.validate());
    serverGlobalParams.overrideShardIdentity = shardIdentity.toShardIdentityDocument();

    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_OK(swShardingInitialized);
    ASSERT_TRUE(swShardingInitialized.getValue());
}

// readOnly and not --shardsvr

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededReadOnlyAndNotShardServerAndNoOverrideShardIdentity) {
    storageGlobalParams.readOnly = true;
    serverGlobalParams.clusterRole = ClusterRole::None;

    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_OK(swShardingInitialized);
    ASSERT_FALSE(swShardingInitialized.getValue());
}

TEST_F(
    ShardingStateTest,
    InitializeShardingAwarenessIfNeededReadOnlyAndNotShardServerAndInvalidOverrideShardIdentity) {
    storageGlobalParams.readOnly = true;
    serverGlobalParams.clusterRole = ClusterRole::None;

    serverGlobalParams.overrideShardIdentity = BSON("_id"
                                                    << "shardIdentity"
                                                    << "configsvrConnectionString"
                                                    << "invalid");
    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_EQUALS(ErrorCodes::InvalidOptions, swShardingInitialized.getStatus().code());
}

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededReadOnlyAndNotShardServerAndValidOverrideShardIdentity) {
    storageGlobalParams.readOnly = true;
    serverGlobalParams.clusterRole = ClusterRole::None;

    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnectionString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName(kShardName);
    shardIdentity.setClusterId(OID::gen());
    ASSERT_OK(shardIdentity.validate());
    serverGlobalParams.overrideShardIdentity = shardIdentity.toShardIdentityDocument();

    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_EQUALS(ErrorCodes::InvalidOptions, swShardingInitialized.getStatus().code());
}

// not readOnly and --overrideShardIdentity

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededNotReadOnlyAndInvalidOverrideShardIdentity) {
    serverGlobalParams.overrideShardIdentity = BSON("_id"
                                                    << "shardIdentity"
                                                    << "configsvrConnectionString"
                                                    << "invalid");

    // Should error regardless of cluster role.

    serverGlobalParams.clusterRole = ClusterRole::ShardServer;
    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_EQUALS(ErrorCodes::InvalidOptions, swShardingInitialized.getStatus().code());

    serverGlobalParams.clusterRole = ClusterRole::None;
    swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_EQUALS(ErrorCodes::InvalidOptions, swShardingInitialized.getStatus().code());
}

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededNotReadOnlyAndValidOverrideShardIdentity) {
    ShardIdentityType shardIdentity;
    shardIdentity.setConfigsvrConnectionString(
        ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
    shardIdentity.setShardName(kShardName);
    shardIdentity.setClusterId(OID::gen());
    ASSERT_OK(shardIdentity.validate());
    serverGlobalParams.overrideShardIdentity = shardIdentity.toShardIdentityDocument();

    // Should error regardless of cluster role.

    serverGlobalParams.clusterRole = ClusterRole::ShardServer;
    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_EQUALS(ErrorCodes::InvalidOptions, swShardingInitialized.getStatus().code());

    serverGlobalParams.clusterRole = ClusterRole::None;
    swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_EQUALS(ErrorCodes::InvalidOptions, swShardingInitialized.getStatus().code());
}

// not readOnly and --shardsvr

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededNotReadOnlyAndShardServerAndNoShardIdentity) {
    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_OK(swShardingInitialized);
    ASSERT_FALSE(swShardingInitialized.getValue());
}

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededNotReadOnlyAndShardServerAndInvalidShardIdentity) {
    // Insert the shardIdentity doc to disk while pretending that we are in "standalone" mode,
    // otherwise OpObserver for inserts will prevent the insert from occurring because the
    // shardIdentity doc is invalid
    {
        ScopedSetStandaloneMode standalone(getServiceContext());

        BSONObj invalidShardIdentity = BSON("_id"
                                            << "shardIdentity"
                                            << ShardIdentity::kShardNameFieldName
                                            << kShardName
                                            << ShardIdentity::kClusterIdFieldName
                                            << OID::gen()
                                            << ShardIdentity::kConfigsvrConnectionStringFieldName
                                            << "invalid");

        _dbDirectClient->insert(NamespaceString::kServerConfigurationNamespace.toString(),
                                invalidShardIdentity);
    }

    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_EQUALS(ErrorCodes::UnsupportedFormat, swShardingInitialized.getStatus().code());
}

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededNotReadOnlyAndShardServerAndValidShardIdentity) {
    // Insert the shardIdentity doc to disk while pretending that we are in "standalone" mode,
    // otherwise OpObserver for inserts will prevent the insert from occurring because the
    // shardIdentity doc is invalid
    {
        ScopedSetStandaloneMode standalone(getServiceContext());

        BSONObj validShardIdentity = [&] {
            ShardIdentityType shardIdentity;
            shardIdentity.setConfigsvrConnectionString(
                ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
            shardIdentity.setShardName(kShardName);
            shardIdentity.setClusterId(OID::gen());
            ASSERT_OK(shardIdentity.validate());
            return shardIdentity.toShardIdentityDocument();
        }();

        _dbDirectClient->insert(NamespaceString::kServerConfigurationNamespace.toString(),
                                validShardIdentity);
    }

    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_OK(swShardingInitialized);
    ASSERT_TRUE(swShardingInitialized.getValue());
}

// not readOnly and not --shardsvr

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededNotReadOnlyAndNotShardServerAndNoShardIdentity) {
    ScopedSetStandaloneMode standalone(getServiceContext());

    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_OK(swShardingInitialized);
    ASSERT_FALSE(swShardingInitialized.getValue());
}

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededNotReadOnlyAndNotShardServerAndInvalidShardIdentity) {
    ScopedSetStandaloneMode standalone(getServiceContext());

    _dbDirectClient->insert(NamespaceString::kServerConfigurationNamespace.toString(),
                            BSON("_id"
                                 << "shardIdentity"
                                 << "configsvrConnectionString"
                                 << "invalid"));

    // The shardIdentity doc on disk, even if invalid, is ignored if the ClusterRole is None. This
    // is to allow fixing the shardIdentity doc by starting without --shardsvr.
    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_OK(swShardingInitialized);
    ASSERT_FALSE(swShardingInitialized.getValue());
}

TEST_F(ShardingStateTest,
       InitializeShardingAwarenessIfNeededNotReadOnlyAndNotShardServerAndValidShardIdentity) {
    ScopedSetStandaloneMode standalone(getServiceContext());

    BSONObj validShardIdentity = [&] {
        ShardIdentityType shardIdentity;
        shardIdentity.setConfigsvrConnectionString(
            ConnectionString(ConnectionString::SET, "a:1,b:2", "config"));
        shardIdentity.setShardName(kShardName);
        shardIdentity.setClusterId(OID::gen());
        ASSERT_OK(shardIdentity.validate());
        return shardIdentity.toShardIdentityDocument();
    }();

    _dbDirectClient->insert(NamespaceString::kServerConfigurationNamespace.toString(),
                            validShardIdentity);

    // The shardIdentity doc on disk is ignored if ClusterRole is None.
    auto swShardingInitialized =
        shardingState()->initializeShardingAwarenessIfNeeded(operationContext());
    ASSERT_OK(swShardingInitialized);
    ASSERT_FALSE(swShardingInitialized.getValue());
}

}  // namespace
}  // namespace mongo
