// database.cpp

/**
*    Copyright (C) 2017 MongoDB Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*    As a special exception, the copyright holders give permission to link the
*    code of portions of this program with the OpenSSL library under certain
*    conditions as described in each individual source file and distribute
*    linked combinations including the program with the OpenSSL library. You
*    must comply with the GNU Affero General Public License in all respects for
*    all of the code used other than as permitted herein. If you modify file(s)
*    with this exception, you may extend this exception to your version of the
*    file(s), but you are not obligated to do so. If you do not wish to do so,
*    delete this exception statement from your version. If you delete this
*    exception statement from all source files in the program, then also delete
*    it in the license file.
*/

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/platform/basic.h"

#include "mongo/db/catalog/database.h"

#include <algorithm>
#include <memory>

#include "mongo/stdx/functional.h"

namespace mongo {
namespace {
static stdx::function<std::unique_ptr<Database::Impl>(
    Database*, OperationContext*, StringData, DatabaseCatalogEntry*)>
    factory;
}  // namespace

auto Database::makeImpl(Database* const this_,
                        OperationContext* const txn,
                        const StringData name,
                        DatabaseCatalogEntry* const dbEntry) -> std::unique_ptr<Impl> {
    return factory(this_, txn, name, dbEntry);
}

void Database::registerImpl(
    stdx::function<std::unique_ptr<Impl>(
        Database*, OperationContext*, StringData, DatabaseCatalogEntry*)> newFactory) {
    factory = std::move(newFactory);
}

auto Database::pimpl() const -> const Impl* {
    assert(this->_pimpl.get());
    return this->_pimpl.get();
}

auto Database::pimpl() -> Impl* {
    assert(this->_pimpl.get());
    return this->_pimpl.get();
}

Database::Impl::~Impl() = default;
}  // namespace mongo

namespace mongo {
namespace {
static stdx::function<Status(
    OperationContext*, Database*, StringData, BSONObj, bool, const BSONObj&)>
    userCreateNSHandler;
}  // namespace
}  // namespace mongo

auto mongo::userCreateNS(OperationContext* const txn,
                         Database* const db,
                         const StringData ns,
                         BSONObj options,
                         const bool createDefaultIndexes,
                         const BSONObj& idIndex) -> Status {
    return userCreateNSHandler(txn, db, ns, std::move(options), createDefaultIndexes, idIndex);
}

void mongo::registerUserCreateNSHandler(
    stdx::function<Status(OperationContext*, Database*, StringData, BSONObj, bool, const BSONObj&)>
        handler) {
    userCreateNSHandler = std::move(handler);
}

namespace mongo {
namespace {
static stdx::function<void(OperationContext*)> dropAllDatabasesExceptLocalHandler;
}  // namespace
}  // namespace mongo

void mongo::dropAllDatabasesExceptLocal(OperationContext* const txn) {
    return dropAllDatabasesExceptLocalHandler(txn);
}


void mongo::registerDropAllDatabasesExceptLocalHandler(
    stdx::function<void(OperationContext*)> handler) {
    dropAllDatabasesExceptLocalHandler = std::move(handler);
}
