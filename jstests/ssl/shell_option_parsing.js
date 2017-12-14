// Test mongo shell connect strings.
(function() {
    'use strict';

    const SERVER_CERT = "jstests/libs/server.pem";
    const CAFILE = "jstests/libs/ca.pem";

    var opts = {
        sslMode: "allowSSL",
        sslPEMKeyFile: SERVER_CERT,
        sslAllowInvalidCertificates: "",
        sslAllowConnectionsWithoutCertificates: "",
        sslCAFile: CAFILE,
        setParameter: "authenticationMechanisms=MONGODB-X509,SCRAM-SHA-1"
    };

    var rst = new ReplSetTest({name: 'sslSet', nodes: 3, nodeOptions: opts});

    rst.startSet();
    rst.initiate();

    const mongod = rst.getPrimary();
    const host = mongod.host;
    const port = mongod.port;

    const username = "user";
    const usernameNotTest = "userNotTest";
    const usernameX509 = "C=US,ST=New York,L=New York City,O=MongoDB,OU=KernelUser,CN=client";

    const password = username;
    const passwordNotTest = usernameNotTest;

    mongod.getDB("test").createUser({user: username, pwd: username, roles: []});
    mongod.getDB("notTest").createUser({user: usernameNotTest, pwd: usernameNotTest, roles: []});
    mongod.getDB("$external").createUser({user: usernameX509, roles: []});

    var i = 0;
    function testConnect(noPasswordPrompt, ...args) {
        const command = [
            'mongo',
            '--eval',
            ';',
            '--ssl',
            '--sslAllowInvalidHostnames',
            '--sslCAFile',
            CAFILE,
            ...args
        ];
        print("=========================================> The command (" + (i++) +
              ") I am going to run is: " + command.join(' '));
        var clientPID = _startMongoProgram.apply(null, command);
        var isRunning = function() {
            return checkProgram(clientPID).alive;
        };

        var isNotRunning =
            function() {
            return !isRunning();
        };

        var terminated = false;
        sleep(1000);
        try {
            if (noPasswordPrompt) {
                sleep(30000);  // ms
                assert(isNotRunning(),
                       "unexpectedly asked for password with `" + command.join(' ') + "`");
                terminated = true;
            } else {
                sleep(30000);  // ms
                terminated = true;
                assert(isRunning(), "failed to ask for password with `" + command.join(' ') + "`");
                terminated = false;
            }
        } finally {
            if (!terminated) {
                stopMongoProgramByPid(clientPID);
            }
        }
    }

    testConnect(false, `mongodb://${username}@${host}/test`);
    testConnect(false, `mongodb://${username}@${host}/test`, '--password');

    testConnect(false, `mongodb://${username}@${host}/test`, '--username', username);
    testConnect(false, `mongodb://${username}@${host}/test`, '--password', '--username', username);

    testConnect(false,
                `mongodb://${usernameNotTest}@${host}/test?authSource=notTest`,
                '--password',
                '--username',
                usernameNotTest);
    testConnect(false,
                `mongodb://${usernameNotTest}@${host}/test?authSource=notTest`,
                '--password',
                '--username',
                usernameNotTest,
                '--authenticationDatabase',
                'notTest');
    testConnect(false,
                `mongodb://${usernameNotTest}@${host}/test`,
                '--password',
                '--username',
                usernameNotTest,
                '--authenticationDatabase',
                'notTest');

    testConnect(false, `mongodb://${host}/test`, '--username', username);
    testConnect(false, `mongodb://${host}/test`, '--password', '--username', username);

    testConnect(true, `mongodb://${host}/test`, '--password', password, '--username', username);

    testConnect(true, `mongodb://${username}:${password}@${host}/test`);
    testConnect(true, `mongodb://${username}:${password}@${host}/test`, '--password');
    testConnect(true, `mongodb://${username}:${password}@${host}/test`, '--password', password);
    testConnect(true, `mongodb://${username}@${host}/test`, '--password', password);

    /* TODO: Enable this set of tests in the future */
    if (false) {
        testConnect(
            true,
            `mongodb://${usernameX509}@${host}/test?authMechanism=MONGODB-X509&authSource=$external`);
        testConnect(
            true,
            `mongodb://${usernameX509}@${host}/test?authMechanism=MONGODB-X509&authSource=$external`,
            '--username',
            usernameX509);
        testConnect(true,
                    `mongodb://${usernameX509}@${host}/test?authSource=$external`,
                    '--authenticationMechanism',
                    'MONGODB-X509');

        testConnect(
            true,
            `mongodb://${usernameX509}@${host}/test?authMechanism=MONGODB-X509&authSource=$external`,
            '--authenticationMechanism',
            'MONGODB-X509');
        testConnect(
            true,
            `mongodb://${usernameX509}@${host}/test?authMechanism=MONGODB-X509&authSource=$external`,
            '--authenticationMechanism',
            'MONGODB-X509',
            '--username',
            usernameX509);
        testConnect(true,
                    `mongodb://${usernameX509}@${host}/test?authSource=$external`,
                    '--authenticationMechanism',
                    'MONGODB-X509');
    }
    /* */

    testConnect(true, `mongodb://${host}/test?authMechanism=MONGODB-X509&authSource=$external`);
    testConnect(true,
                `mongodb://${host}/test?authMechanism=MONGODB-X509&authSource=$external`,
                '--username',
                usernameX509);

    testConnect(true,
                `mongodb://${host}/test?authSource=$external`,
                '--authenticationMechanism',
                'MONGODB-X509');
    testConnect(true,
                `mongodb://${host}/test?authSource=$external`,
                '--username',
                usernameX509,
                '--authenticationMechanism',
                'MONGODB-X509');
})();
