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

#include <unistd.h>

#include <stdlib.h>

#include <algorithm>
#include <exception>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#include "mongo/stdx/testing/thread_helpers.h"
#include "mongo/stdx/thread.h"

namespace {
namespace stdx = ::mongo::stdx;

const int kSignal = SIGINFO;

std::atomic<bool> blockage{true};
std::atomic<const void*> handlerStack;
std::atomic<const void*> threadStack;

void recurse(const int n) {
    if (n == 10) {
        raise(kSignal);
        while (blockage)
            ;
    } else
        recurse(n + 1);
}

void handler(const int n) {
    handlerStack = static_cast<const void*>(&n);
    blockage = false;
}

void installSignalHandler() {
    struct sigaction action {};
    action.sa_handler = handler;
    if constexpr (stdx::support::SignalStack::kEnabled)
        action.sa_flags = SA_ONSTACK;
    else
        action.sa_flags = 0;

    sigemptyset(&action.sa_mask);
    const auto ec = ::sigaction(kSignal, &action, nullptr);
    const int myErrno = errno;
    if (ec != 0) {
        std::cout << "sigaction failed: " << ec << " and errno is: " << myErrno << std::endl;
        exit(EXIT_FAILURE);
    }
}

void setupSignalMask() {
    sigset_t sigset;
    sigemptyset(&sigset);
    const auto ec = sigprocmask(SIG_UNBLOCK, &sigset, nullptr);
    const int myErrno = errno;
    if (ec != 0) {
        std::cout << "sigprocmask failed: " << ec << " and errno is: " << myErrno << std::endl;
        exit(EXIT_FAILURE);
    }
}


std::mutex thrmtx;
std::condition_variable cv;

enum InterlockedThreadState { kNone, kHandlerRun, kRetireChild } interlockedThreadState = kNone;

void jumpoff() {
    auto lk = std::unique_lock(thrmtx);
    threadStack = &lk;

    setupSignalMask();
    installSignalHandler();

    recurse(0);
    interlockedThreadState = kHandlerRun;
    cv.notify_one();
    cv.wait(lk, [] { return interlockedThreadState == kRetireChild; });
}

}  // namespace


int main() try {
    if constexpr (!stdx::support::SignalStack::kEnabled) {
        std::cout << "No test to run.  No alternate signal stacks on this platform." << std::endl;
        return EXIT_SUCCESS;
    }
    using mongo::stdx::testing::ThreadInformation;
    auto listener = ThreadInformation::Registrar::create();

    auto lk = std::unique_lock(thrmtx);
    stdx::thread thr(jumpoff);
    const auto id = thr.get_id();
    cv.wait(lk, [] { return interlockedThreadState == kHandlerRun; });

    const auto [base, amt] = listener->getMapping(thr.get_id()).altStack;

    const auto bHandlerStack = static_cast<const std::byte*>(handlerStack.load());
    const auto bBase = static_cast<const std::byte*>(base);

    if (!(bBase <= bHandlerStack && bHandlerStack < (bBase + amt))) {
        std::cout << "Handler address was out of altstack bounds (addr, range): " << bHandlerStack
                  << ", [" << bBase << ", " << bBase + amt << ")" << std::endl;
        exit(EXIT_FAILURE);
    }

    const auto bThreadAddress = static_cast<const std::byte*>(threadStack.load());
    if (bBase <= bThreadAddress && bThreadAddress < (bBase + amt)) {
        std::cout << "Child thread address was found on the altstack: " << bThreadAddress << ", ["
                  << bBase << ", " << bBase + amt << ")" << std::endl;
        exit(EXIT_FAILURE);
    }

    interlockedThreadState = kRetireChild;
    lk.unlock();
    cv.notify_one();
    thr.join();

    try {
        listener->getMapping(id);
        std::cerr << "Identifier " << id << " was found, which wasn't expected." << std::endl;
        exit(EXIT_FAILURE);
    } catch (const std::out_of_range&) {
    }
    std::cout << "`sigaltstack` testing successful." << std::endl;
    return EXIT_SUCCESS;
} catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    throw;
}
