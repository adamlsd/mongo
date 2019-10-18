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

#include <map>
#include <mutex>

namespace mongo::stdx::testing {
/**
 * This class is a threadsafe `testing::ThreadInformation::Listener` implementation.  This listener
 * will maintain a table of currently active threads and their `testing::ThreadInformation`.
 *
 * This class uses `std::thread` and other `std::` things throughout, as it is used in testing
 * `stdx::` implementations of those built on top of the `std::` things.  This class needs
 * access to threading operations other than the `stdx::` implementations.
 */
class ThreadInformation::Registrar : ThreadInformation::Listener {
private:
    mutable std::mutex _access;
    std::map<std::thread::id, ThreadInformation> _mapping;

    explicit Registrar() = default;

public:
    ~Registrar() {
        ThreadInformation::Listener::remove(*this);
    }

    static auto create() {
        std::unique_ptr<Registrar> rv(new Registrar{});
        ThreadInformation::Listener::add(*rv);
        return rv;
    }

    void born(const std::thread::id& id, const ThreadInformation& info) override {
        const auto lock = std::lock_guard(_access);
        _mapping[id] = info;
    }

    void died(const std::thread::id& id) override {
        const auto lock = std::lock_guard(_access);
        _mapping.erase(id);
    }

    /**
     * Returns the `ThreadInformation` associated with the `id` parameter.
     */
    ThreadInformation getMapping(const stdx::thread::id& id) const {
        const auto lock = std::lock_guard(_access);
        return _mapping.at(id);
    }
};
}  // namespace mongo::stdx::testing
