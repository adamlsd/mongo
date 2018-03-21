// Tests the copydb command on mongos with auth
(function() {
    'use strict';
    load('jstests/libs/feature_compatibility_version.js');

    // TODO: Remove 'shardAsReplicaSet: false' when SERVER-32672 is fixed.
    var st = new ShardingTest(
        {shards: 1, mongos: 1, other: {keyFile: 'jstests/libs/key1', shardAsReplicaSet: false}});
    var mongos = st.s0;
    var destAdminDB = mongos.getDB('admin');
    var destTestDB = mongos.getDB('test');

    var sourceMongodConn = MongoRunner.runMongod({});
    var sourceTestDB = sourceMongodConn.getDB('test');

    // Ensure sourceMongodConn has featureCompatibilityVersion=lastStableFCV so that the sharded
    // cluster can communicate with it if it has featureCompatibilityVersion=lastStableFCV
    assert.commandWorked(
        sourceMongodConn.adminCommand({setFeatureCompatibilityVersion: lastStableFCV}));

    sourceTestDB.foo.insert({a: 1});

    destAdminDB.createUser({
        user: 'admin',
        pwd: 'password',
        roles: jsTest.adminUserRoles
    });  // Turns on access control enforcement

    jsTestLog("Running copydb that should fail");
    var res = destAdminDB.runCommand(
        {copydb: 1, fromhost: sourceMongodConn.host, fromdb: 'test', todb: 'test'});
    printjson(res);
    assert.commandFailed(res);

    destAdminDB.auth('admin', 'password');
    assert.eq(0, destTestDB.foo.count());  // Be extra sure the copydb didn't secretly succeed.

    jsTestLog("Running copydb that should succeed");
    res = destAdminDB.runCommand(
        {copydb: 1, fromhost: sourceMongodConn.host, fromdb: 'test', todb: 'test'});
    printjson(res);
    assert.commandWorked(res);

    assert.eq(1, destTestDB.foo.count());
    assert.eq(1, destTestDB.foo.findOne().a);

    st.stop();
    MongoRunner.stopMongod(sourceMongodConn);
})();
