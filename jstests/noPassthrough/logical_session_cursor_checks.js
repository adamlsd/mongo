(function() {
    'use strict';

    var conn = MongoRunner.runMongod({auth: "", nojournal: ""});
    var admin = conn.getDB("admin");
    var data = conn.getDB("data_storage");

    admin.createUser({user: 'admin', pwd: 'admin', roles: jsTest.adminUserRoles});
    admin.auth("admin", "admin");
    data.createUser({user: 'user0', pwd: 'password', roles: jsTest.basicUserRoles});
    admin.logout();

    data.auth("user0", "password");
    assert.writeOK(data.test.insert({name: "first", data: 1}));
    assert.writeOK(data.test.insert({name: "second", data: 2}));

    // Test that getMore works correctly on the same session.
    {
        var session1 = conn.startSession();
        var session2 = conn.startSession();
        var res = assert.commandWorked(
            session1.getDatabase("data_storage").runCommand({find: "test", batchSize: 0}));
        var cursorId = res.cursor.id;
        assert.commandWorked(session1.getDatabase("data_storage")
                                 .runCommand({getMore: cursorId, collection: "test"}));

        session2.endSession();
        session1.endSession();
    }

    // Test that getMore correctly gives an error, when using a cursor on a different session.
    {
        var session1 = conn.startSession();
        var session2 = conn.startSession();
        var res = assert.commandWorked(
            session1.getDatabase("data_storage").runCommand({find: "test", batchSize: 0}));
        var cursorId = res.cursor.id;
        assert.commandFailed(session2.getDatabase("data_storage")
                                 .runCommand({getMore: cursorId, collection: "test"}));

        session2.endSession();
        session1.endSession();
    }

    // Test that getMore correctly gives an error, when using a cursor on a different session.
    {
        var session1 = conn.startSession();
        var mongouri = conn.host;
        var adminconn = new Mongo(mongouri);
        var admin = adminconn.getDB("data_storage")
        admin.getSiblingDB("admin").auth("admin", "admin");
        var res = admin.getSiblingDB().aggregate([{'$listLocalSessions': {users: [{user: "user1", db: "test"}]}}])
        print(res.toArray());
        assert.eq(res.toArray().length, 0)
        var cursor = session1.getDatabase("data_storage").test.find().batchSize(0);
        var res2 = admin.getSiblingDB().aggregate([{'$listLocalSessions': {users: [{user: "user1", db: "test"}]}}])
        cursor.next();
        print(res2.toArray());
        assert.eq(res2.toArray().length, 1)
        cursor.next();
        cursor.close();

        session1.endSession();
    }

    MongoRunner.stopMongod(conn);
})();
