(function() {
    'use strict';

    var conn = MongoRunner.runMongod({auth: "", nojournal: ""});
    var admin = conn.getDB("admin");
    var data = conn.getDB("data_storage");

    admin.createUser({user: 'admin', pwd: 'admin', roles: jsTest.adminUserRoles});
    admin.auth("admin", "admin");
    data.createUser({user: 'admin', pwd: 'admin', roles: jsTest.basicUserRoles});
    data.createUser({user: 'user0', pwd: 'password', roles: jsTest.basicUserRoles});
    data.createUser({user: 'user1', pwd: 'password', roles: jsTest.basicUserRoles});
    admin.logout();


    data.auth("user0", "password");
    assert.writeOK(data.test.insert({name: "first", data: 1}));
    assert.writeOK(data.test.insert({name: "second", data: 2}));

    {
		var session1 = conn.startSession();
		var session2 = conn.startSession();
        res = assert.commandWorked(session1.getDatabase("data_storage").runCommand({find: "test", batchSize: 0}));
        var cursorId = res.cursor.id;
        assert.commandWorked(session1.getDatabase("data_storage").runCommand({getMore: cursorId, collection: "test"}));
        assert.commandFailed(session2.getDatabase("data_storage").runCommand({getMore: cursorId, collection: "test"}));

		session2.endSession();
		session1.endSession();
    }

    MongoRunner.stopMongod(conn);
})();

