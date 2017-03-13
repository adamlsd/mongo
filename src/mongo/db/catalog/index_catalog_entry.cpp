// index_catalog_entry.cpp

/**
*    Copyright (C) 2017 10gen Inc.
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

#include "mongo/db/index/index_descriptor.h"
#include "mongo/db/index/index_access_method.h"

namespace mongo
{
	IndexCatalogEntry::Impl::~Impl()= default;

	namespace
	{
		stdx::function< std::unique_ptr< IndexCatalogEntry::Impl >( IndexCatalogEntry *, OperationContext *, StringData, CollectionCatalogEntry *, std::unique_ptr< IndexDescriptor >, CollectionInfoCache * ) > factory;
	}

	void
	IndexCatalogEntry::registerFactory( decltype( factory ) newFactory ) { factory= std::move( newFactory ); }

	auto
	IndexCatalogEntry::makeImpl( IndexCatalogEntry *const this_, OperationContext *const opCtx, const StringData ns, CollectionCatalogEntry *const collection, std::unique_ptr< IndexDescriptor > descriptor, CollectionInfoCache *const infoCache )
	->std::unique_ptr< Impl >
	{
		return factory( this_, opCtx, ns, collection, std::move( descriptor ), infoCache );
	}

	auto
	IndexCatalogEntry::impl() const->const Impl &{ return *this->_pimpl; }

	auto
	IndexCatalogEntry::impl()->Impl & { return *this->_pimpl; }

	IndexCatalogEntry::IndexCatalogEntry( OperationContext *opCtx, StringData ns, CollectionCatalogEntry *collection, std::unique_ptr< IndexDescriptor > descriptor, CollectionInfoCache *infoCache )
		: _pimpl( makeImpl( this, opCtx, ns, collection, std::move( descriptor ), infoCache ) ) {}

	void
	IndexCatalogEntry::init ( std::unique_ptr< IndexAccessMethod > accessMethod ) { return this->impl().init( std::move( accessMethod ) ); }
}	// namespace mongo
