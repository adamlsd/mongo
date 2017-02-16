// index_catalog.cpp

/**
*    Copyright (C) 2013-2014 MongoDB Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kIndex

#include "mongo/platform/basic.h"

#include "mongo/db/catalog/index_catalog.h"

namespace mongo {
namespace {
stdx::function<std::unique_ptr<IndexCatalog::IndexIterator::Impl>(
    OperationContext*, const IndexCatalog*, bool)>
    iteratorFactory;
}  // namespace

auto IndexCatalog::IndexIterator::makeImpl(OperationContext* const txn,
                                           const IndexCatalog* const cat,
                                           const bool includeUnfinishedIndexes)
    -> std::unique_ptr<Impl> {
    return iteratorFactory(txn, cat, includeUnfinishedIndexes);
}

void IndexCatalog::IndexIterator::registerFactory(
    stdx::function<std::unique_ptr<Impl>(OperationContext*, const IndexCatalog*, bool)>
        newFactory) {
    iteratorFactory = std::move(newFactory);
}

// Emit the vtable for this class in this TU.
IndexCatalog::IndexIterator::Impl::~Impl() = default;

namespace {
stdx::function<std::unique_ptr<IndexCatalog::IndexBuildBlock::Impl>(
    OperationContext*, Collection*, const BSONObj&)>
    buildBlockFactory;
}  // namespace

auto IndexCatalog::IndexBuildBlock::makeImpl(OperationContext* txn,
                                             Collection* collection,
                                             const BSONObj& spec) -> std::unique_ptr<Impl> {
    return buildBlockFactory(txn, collection, spec);
}

void IndexCatalog::IndexBuildBlock::registerFactory(
    stdx::function<std::unique_ptr<Impl>(OperationContext*, Collection*, const BSONObj&)> factory) {
    buildBlockFactory = std::move(factory);
}

// Emit the vtable for this class in this TU.
IndexCatalog::IndexBuildBlock::Impl::~Impl() = default;

namespace {
stdx::function<std::unique_ptr<IndexCatalog::Impl>(Collection*)> catalogFactory;
}  // namespace

auto IndexCatalog::makeImpl(Collection* const collection) -> std::unique_ptr<Impl> {
    return catalogFactory(collection);
}

void IndexCatalog::registerFactory(stdx::function<std::unique_ptr<Impl>(Collection*)> factory) {
    catalogFactory = std::move(factory);
}

// Emit the vtable for this class in this TU.
IndexCatalog::Impl::~Impl() = default;
}  // namespace mongo
