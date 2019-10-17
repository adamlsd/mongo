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

#include "mongo/stdx/thread.h"

#include <mutex>
#include <map>

namespace mongo::stdx::testing {
/**
 * This class is a threadsafe `testing::ThreadInformation::Listener` implementation.  This listener
 * will maintain a table of currently active threads and their `testing::ThreadInformation`.
 */
class ThreadInformation::Registrar : mongo::stdx::testing::ThreadInformation::Listener {
private:
    mutable std::mutex mtx;
    std::map<std::thread::id, ThreadInformation> mapping;

    // Since we create this class by `std::make_unique`, but the constructor itself has to be
    // public, we use this protected token type to prevent external callers from creating a
    // `Registar` instance, without notifying the `ThreadInformation::Listener` framework about it.
    // This is necessary as the fully constructed `Registrar` class must be provided to the
    // `ThreadInformation::Listener` mechanism via the `Registar::create` factory function.
    class protected_constructor {
        explicit protected_constructor() = default;
        friend Registrar;
    };


public:
    explicit Registrar(protected_constructor) {}

    ~Registrar() {
        ThreadInformation::Listener::remove(*this);
    }

    static auto create() {
        auto rv = std::make_unique<Registrar>(protected_constructor{});
        ThreadInformation::Listener::add(*rv);
        return rv;
    }


    void activate(const std::thread::id& id, const ThreadInformation& info) override {
        const auto lk = std::lock_guard(mtx);
        assert(!mapping.count(id));
        mapping[id] = info;
    }

    void quiesce(const std::thread::id& id) override {
        const auto lk = std::lock_guard(mtx);
        mapping.erase(id);
    }

    /**
     * Returns the `ThreadInformation` associated with the `id` parameter.
     */
    ThreadInformation getMapping(const stdx::thread::id& id) const {
        const auto lk = std::lock_guard(mtx);
        return mapping.at(id);
    }
};
}  // namespace mongo::stdx::testing
