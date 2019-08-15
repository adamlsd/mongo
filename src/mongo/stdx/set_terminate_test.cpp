/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */


#include "mongo/stdx/exception.h"

#include "mongo/unittest/unittest.h"

#include <sys/types.h>

#include <sys/wait.h>

#include <iostream>
#include <unistd.h>

#include "mongo/stdx/thread.h"

namespace {
namespace stdx = mongo::stdx;

const int message = 42;
int pipedes[2];

void writeFeedbackAndCleanlyExit() {
    write(pipedes[1], &message, sizeof(message));
    std::exit(0);
}

TEST(SetTerminateTest, testTerminateDispatch) {
    ASSERT(pipe(pipedes) == 0);


    const int pid = fork();
    if (!pid) {
        close(pipedes[0]);
        stdx::set_terminate(writeFeedbackAndCleanlyExit);
        std::terminate();
    }

    close(pipedes[1]);

    int status;
    waitpid(pid, &status, 0);

    int receipt = 0;
    ASSERT_GT(read(pipedes[0], &receipt, sizeof(receipt)), 0);

    ASSERT_EQ(receipt, message);
    close(pipedes[0]);
}

TEST(SetTerminateTest, testTerminateStdDispatch) {
    ASSERT(pipe(pipedes) == 0);

    const int pid = fork();

    if (!pid) {
        close(pipedes[0]);
        std::set_terminate(writeFeedbackAndCleanlyExit);
        std::terminate();
    }
    close(pipedes[1]);

    int status;
    waitpid(pid, &status, 0);

    int receipt = 0;
    ASSERT_GT(read(pipedes[0], &receipt, sizeof(receipt)), 0);

    ASSERT_EQ(receipt, message);
    close(pipedes[0]);
}

#if 1
TEST(SetTerminateTest, testTerminateNonDispatch) {
    ASSERT(pipe(pipedes) == 0);

    const int pid = fork();

    if (!pid) {
        close(pipedes[0]);
        std::terminate();
    }
    close(pipedes[1]);

    int status;
    waitpid(pid, &status, 0);

    int receipt = 0;
    ASSERT_LTE(read(pipedes[0], &receipt, sizeof(receipt)), 0);
}
#endif

TEST(SetTerminateTest, setFromMainDieInThread) {
    ASSERT(pipe(pipedes) == 0);

    const int pid = fork();

    if (!pid) {
        close(pipedes[0]);
        stdx::set_terminate(writeFeedbackAndCleanlyExit);
        stdx::thread bg([] { std::terminate(); });

        bg.join();
        ASSERT(false);
    }
    close(pipedes[1]);

    int status;
    waitpid(pid, &status, 0);

    int receipt = 0;
    ASSERT_GT(read(pipedes[0], &receipt, sizeof(receipt)), 0);

    ASSERT_EQ(receipt, message);
    close(pipedes[0]);
}

TEST(SetTerminateTest, setFromThreadDieInMain) {
    ASSERT(pipe(pipedes) == 0);

    const int pid = fork();

    if (!pid) {
        close(pipedes[0]);
        stdx::thread bg([] { stdx::set_terminate(writeFeedbackAndCleanlyExit); });
        bg.join();

        std::terminate();

        ASSERT(false);
    }
    close(pipedes[1]);

    int status;
    waitpid(pid, &status, 0);

    int receipt = 0;
    ASSERT_GT(read(pipedes[0], &receipt, sizeof(receipt)), 0);

    ASSERT_EQ(receipt, message);
    close(pipedes[0]);
}


TEST(SetTerminateTest, setFromThreadDieInThread) {
    ASSERT(pipe(pipedes) == 0);

    const int pid = fork();

    if (!pid) {
        close(pipedes[0]);
        stdx::thread bg([] { stdx::set_terminate(writeFeedbackAndCleanlyExit); });
        bg.join();
        stdx::thread bg2([] { std::terminate(); });
        bg2.join();

        ASSERT(false);
    }
    close(pipedes[1]);

    int status;
    waitpid(pid, &status, 0);

    int receipt = 0;
    ASSERT_GT(read(pipedes[0], &receipt, sizeof(receipt)), 0);

    ASSERT_EQ(receipt, message);
    close(pipedes[0]);
}
}  // namespace
