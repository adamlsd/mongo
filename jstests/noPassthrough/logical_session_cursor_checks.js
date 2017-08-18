(function() {
    'use strict';

    var conn = MongoRunner.runMongod({nojournal: ""});
    var admin = conn.getDB("admin");

    conn = MongoRunner.runMongod({auth: "", nojournal: ""});
    admin = conn.getDB("admin");
    data = conn.getDB("data_storage");

    admin.createUser({user: 'admin', pwd: 'admin', roles: jsTest.adminUserRoles});
    admin.auth("admin", "admin");
    data.createUser({user: 'admin', pwd: 'admin', roles: jsTest.basicUserRoles});
    data.createUser({user: 'user0', pwd: 'password', roles: jsTest.basicUserRoles});
    data.createUser({user: 'user1', pwd: 'password', roles: jsTest.basicUserRoles});
    admin.logout();

    data.auth("user0", "password");
    assert.writeOK(data.insert({name: "first", data: 1}));
    assert.writeOK(data.insert({name: "second", data: 2}));

    var res = assert.commandWorked(data.runCommand({startSession: 1}));
	var sessionId = res.id;
    res = assert.commandWorked(data.runCommand({find: "data_storage", batchSize: 0}));
    var cursorId = res.cursor.id;
    assert.commandWorked(testDB.runCommand({getMore: cursorId, collection: "data_storage"}));

    assert.commandWorked(data.runCommand({startSession: 2}));
    assert.commandFailedWithCode(data2.runCommand({getMore: cursorId, collection: "data_storage"}),
                                 ErrorCodes.Unauthorized);

    MongoRunner.stopMongod(conn);
})();

