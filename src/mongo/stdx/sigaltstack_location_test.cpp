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

const auto kSignalNumber = SIGINFO;

std::atomic<bool> blockage{true};
std::atomic<void*> address;

void recurse(const int n) {
    if (n == 10) {
        raise(kSignalNumber);
        while (blockage)
            ;
        std::cerr << "Unblocked" << std::endl;
    } else
        recurse(n + 1);
}

void handler(const int n) {
    std::cerr << "Handler called." << std::endl;
    address = (void*)&n;
    blockage = false;
}

void installSignalHandler() {
    struct sigaction action {};
    action.sa_handler = handler;
    if constexpr (stdx::thread::usingSigaltstacks)
        action.sa_flags = SA_ONSTACK;
    else
        action.sa_flags = 0;

    sigemptyset(&action.sa_mask);
    const auto ec = ::sigaction(kSignalNumber, &action, nullptr);
    const int myErrno = errno;
    if (ec != 0) {
        std::cerr << "Got ec: " << ec << " and errno is: " << myErrno << std::endl;
        exit(EXIT_FAILURE);
    }
}

void setupSignalMask() {
    sigset_t sigset;
    sigemptyset(&sigset);
    const auto ec = sigprocmask(SIG_UNBLOCK, &sigset, nullptr);
    const int myErrno = errno;
    if (ec != 0) {
        std::cerr << "Got ec: " << ec << " and errno is: " << myErrno << std::endl;
        exit(EXIT_FAILURE);
    }
}


std::mutex thrmtx;
std::condition_variable cv;
std::atomic<void*> mainAddress;

void jumpoff() {
    auto lk = std::unique_lock(thrmtx);
    mainAddress = &lk;

    setupSignalMask();
    installSignalHandler();

    recurse(0);
    std::cerr << "Recurse done" << std::endl;
    cv.notify_one();
    std::cerr << "Parent notified" << std::endl;
    cv.wait(lk);
    std::cerr << "Resumed after parent notification." << std::endl;

    std::cerr << "Thread done" << std::endl;
}

}  // namespace


int main() try {
    if constexpr (!stdx::thread::usingSigaltstacks)
        return EXIT_SUCCESS;
    mongo::stdx::testing::ThreadInformationListener listener;

    auto lk = std::unique_lock(thrmtx);
    stdx::thread thr(jumpoff);
    const auto id = thr.get_id();
    cv.wait(lk);
    std::cerr << "The parent has received child notification" << std::endl;
    // const auto [ pos, amt ]= mongo::getInformationForThread( thr );
    std::cerr << "The parent is looking to the listener" << std::endl;
    const auto [base, amt] = listener.getMapping(thr).altStack;
    std::cerr << "The parent is checking layouts" << std::endl;

    const auto bAddress = static_cast<const std::byte*>(static_cast<void*>(address));
    const auto bBase = static_cast<const std::byte*>(base);
    if (!(bBase <= bAddress && bAddress < (bBase + amt))) {
        std::cout << "Address was out of bounds (addr, range): " << bAddress << ", [" << bBase
                  << ", " << bBase + amt << ")" << std::endl;
        exit(EXIT_FAILURE);
    }

    const auto bmAddress = static_cast<const std::byte*>(static_cast<void*>(mainAddress));
    if (bBase <= bmAddress && bmAddress < (bBase + amt)) {
        std::cout << "Main address was not out of bounds" << std::endl;
        exit(EXIT_FAILURE);
    }

    lk.unlock();
    cv.notify_one();
    thr.join();

    try {
        listener.getMapping(id);
        std::cerr << "Identifier " << id << " was found, which wasn't expected." << std::endl;
        exit(EXIT_FAILURE);
    } catch (const std::out_of_range&) {
    }
    return EXIT_SUCCESS;
} catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    throw;
}
