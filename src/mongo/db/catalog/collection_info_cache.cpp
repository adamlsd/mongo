// collection_info_cache.cpp
// collection_info_cache.h

/**
*    Copyright (C) 2013 10gen Inc.
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

#include "mongo/db/catalog/collection_info_cache.h"

#include "mongo/db/collection_index_usage_tracker.h"
#include "mongo/db/query/plan_cache.h"
#include "mongo/db/query/query_settings.h"
#include "mongo/db/update_index_data.h"
#include "mongo/stdx/functional.h"

namespace mongo {
namespace {
stdx::function<std::unique_ptr<CollectionInfoCache::Impl>(Collection* collection)> implFactory;
}

auto CollectionInfoCache::makeImpl(Collection* const collection) -> std::unique_ptr<Impl> {
    return implFactory(collection);
}

void CollectionInfoCache::registerImpl(
    stdx::function<std::unique_ptr<Impl>(Collection* collection)> factory) {
    implFactory = std::move(factory);
}


const CollectionInfoCache::Impl* CollectionInfoCache::pimpl() const {
    return this->_pimpl.get();
}

CollectionInfoCache::Impl* CollectionInfoCache::pimpl() {
    return this->_pimpl.get();
}

CollectionInfoCache::Impl::~Impl() = default;
}  // namespace mongo
