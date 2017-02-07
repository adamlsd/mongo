// index_catalog_entry.cpp

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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kIndex

#include "mongo/platform/basic.h"

#include "mongo/db/catalog/index_catalog_entry.h"

#include <algorithm>

#include "mongo/db/catalog/collection_catalog_entry.h"
#include "mongo/db/catalog/head_manager.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/index/index_access_method.h"
#include "mongo/db/index/index_descriptor.h"
#include "mongo/db/matcher/expression.h"
#include "mongo/db/matcher/expression_parser.h"
#include "mongo/db/matcher/extensions_callback_disallow_extensions.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/query/collation/collator_factory_interface.h"
#include "mongo/db/service_context.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {
namespace {
std::function<std::unique_ptr<IndexCatalogEntry::Impl>(OperationContext* txn,
                                                       StringData ns,
                                                       CollectionCatalogEntry* collection,
                                                       IndexDescriptor* descriptor,
                                                       CollectionInfoCache* infoCache)>
    factory;

}  // namespace

auto IndexCatalogEntry::makeImpl(OperationContext* const txn,
                                 const StringData ns,
                                 CollectionCatalogEntry* const collection,
                                 IndexDescriptor* const descriptor,
                                 CollectionInfoCache* const infoCache)

    -> std::unique_ptr<Impl> {
    return factory(txn, ns, collection, descriptor, infoCache);
}

void IndexCatalogEntry::registerFactory(
    stdx::function<std::unique_ptr<Impl>(OperationContext*,
                                         StringData,
                                         CollectionCatalogEntry*,
                                         IndexDescriptor*,
                                         CollectionInfoCache* infoCache)> newFactory) {
    factory = std::move(newFactory);
}

// Force the vtable to be emitted in this TU.
IndexCatalogEntry::Impl::~Impl() = default;

// A cyclic class member reference and inclusion structure forces this to be emitted in a last-in-TU
// fashion
void IndexCatalogEntry::init(std::unique_ptr<IndexAccessMethod> accessMethod) {
    this->pimpl->init(std::move(accessMethod));
}
}  // namespace mongo
