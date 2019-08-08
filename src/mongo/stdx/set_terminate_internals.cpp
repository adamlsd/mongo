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

#include <atomic>
#include <utility>


namespace mongo {
namespace stdx::terminate_detail {
static void dispatch_impl() noexcept;

namespace {
::std::atomic<::std::terminate_handler> terminationHandler = []() noexcept {
    return ::std::set_terminate(&dispatch_impl);
}
();
}  // namespace
}  // namespace stdx::terminate_detail

void stdx::terminate_detail::dispatch_impl() noexcept {
    if (const ::std::terminate_handler handler = terminationHandler)
        handler();
}

void stdx::terminate_detail::TerminateHandlerInterface::dispatch() noexcept {
    return stdx::terminate_detail::dispatch_impl();
}

stdx::terminate_handler stdx::set_terminate(const terminate_handler handler) noexcept {
    return terminate_detail::terminationHandler.exchange(handler);
}

stdx::terminate_handler stdx::get_terminate() noexcept {
    return terminate_detail::terminationHandler;
}
}  // namespace mongo
