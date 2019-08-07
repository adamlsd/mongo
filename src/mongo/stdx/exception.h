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

#pragma once

#include <exception>
#include <atomic>
#include <utility>

// This file provides a wrapper over the function registered by `std::set_terminate`.
// This facilitates making `stdx::set_terminate` work correctly on windows.  In
// windows, `std::set_terminate` works on a per-thread basis.  Our `stdx::thread`
// header registers the `stdx::terminate_detail::termination_handler` with
// `std::set_terminate` when a thread starts on windows.  `stdx::set_terminate`
// sets the handler globally for all threads.  Our wrapper, which is registered
// with each thread, calls the global handler.

namespace mongo::stdx {
::std::terminate_handler set_terminate(::std::terminate_handler) noexcept;
::std::terminate_handler get_terminate() noexcept;

class thread;

namespace terminate_detail {
class TerminateHandlerInterface {
    friend ::mongo::stdx::thread;
    friend decltype(::mongo::stdx::set_terminate) mongo::stdx::set_terminate;
    friend decltype(::mongo::stdx::get_terminate) mongo::stdx::get_terminate;

    static void dispatch() noexcept {
        if (const ::std::terminate_handler handler = TerminateHandlerStorage::terminationHandler)
            handler();
    }

    class TerminateHandlerStorage {
        friend TerminateHandlerInterface;
        friend decltype(::mongo::stdx::set_terminate) mongo::stdx::set_terminate;
        friend decltype(::mongo::stdx::get_terminate) mongo::stdx::get_terminate;

        // We need to initialize the global terminate handler for the main thread as
        // early as possible, even possibly before MONGO_INITIALIZERS.  The built-in
        // termination handler is just forwarded from our wrapper.  This is a static
        // initializer of a value with a side effect.
        inline static ::std::atomic<::std::terminate_handler> terminationHandler = []() noexcept {
            return ::std::set_terminate(
                ::mongo::stdx::terminate_detail::TerminateHandlerInterface::dispatch);
        }
        ();
    };
};
}  // namespace terminate_detail

using ::std::terminate_handler;

inline terminate_handler set_terminate(const terminate_handler newHandler) noexcept {
    using Storage = terminate_detail::TerminateHandlerInterface::TerminateHandlerStorage;
    return Storage::terminationHandler.exchange( newHandler);
}

inline terminate_handler get_terminate() noexcept {
    return terminate_detail::TerminateHandlerInterface::TerminateHandlerStorage::terminationHandler;
}
}  // namespace mongo::stdx
