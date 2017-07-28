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

#include "mongo/db/db_raii.h"

namespace mongo
{
	AutoGetDb::Impl::~Impl()= default;

	void AutoGetDb::TUHook::hook() noexcept {}

	namespace
	{
		class EvilAbort
		{
			public:
				explicit EvilAbort()= default;

				template< typename Rv, typename ... Args >
				Rv
				operator () () { abort(); }

				template< typename Rv, typename ... Args >
				operator std::function< std::unique_ptr< Rv > ( Args ... ) > ()
				{
					return *this;
				}
		};
	}//namespace

	namespace
	{
		namespace auto_get_db
		{
			AutoGetDb::factory_function_type factory= EvilAbort{};
		}//namespace auto_get_db
	}//namespace

	void
	AutoGetDb::registerFactory( factory_function_type new_factory )
	{
		using namespace auto_get_db;
		factory= std::move( new_factory );
	}

	auto
	AutoGetDb::makeImpl( OperationContext *const opCtx, const StringData ns, const LockMode mode )
		-> std::unique_ptr< Impl >
	{
		using namespace auto_get_db;
		return factory( opCtx, ns, mode );
	}


	AutoGetCollection::Impl::~Impl()= default;

	void AutoGetCollection::TUHook::hook() noexcept {}

	namespace
	{
		namespace auto_get_collection
		{
			AutoGetCollection::factory_function_type factory= EvilAbort{};
		}//namespace auto_get_collection
	}//namespace

	void
	AutoGetCollection::registerFactory( factory_function_type new_factory )
	{
		using namespace auto_get_collection;
		factory= std::move( new_factory );
	}

	auto
	AutoGetCollection::makeImpl( OperationContext *const opCtx, const NamespaceString &nss, const LockMode modeDB, const LockMode modeColl, const ViewMode viewMode )
			-> std::unique_ptr< Impl >
	{
		using namespace auto_get_collection;
		return factory( opCtx, nss, modeDB, modeColl, viewMode );
	}



	AutoGetCollectionOrView::Impl::~Impl()= default;

	void AutoGetCollectionOrView::TUHook::hook() noexcept {}

	namespace
	{
		namespace auto_get_collection_or_view
		{
			AutoGetCollectionOrView::factory_function_type factory= EvilAbort{};
		}//namespace auto_get_collection_or_view
	}//namespace

	void
	AutoGetCollectionOrView::registerFactory( factory_function_type new_factory )
	{
		using namespace auto_get_collection_or_view;
		factory= std::move( new_factory );
	}

	auto
	AutoGetCollectionOrView::makeImpl( OperationContext *const opCtx, const NamespaceString &nss, const LockMode modeAll )
			-> std::unique_ptr< Impl >
	{
		using namespace auto_get_collection_or_view;
		return factory( opCtx, nss, modeAll );
	}



	AutoGetOrCreateDb::Impl::~Impl()= default;

	void AutoGetOrCreateDb::TUHook::hook() noexcept {}

	namespace
	{
		namespace auto_get_or_create_db
		{
			AutoGetOrCreateDb::factory_function_type factory= EvilAbort{};
		}//namespace auto_get_or_create_db
	}//namespace

	void
	AutoGetOrCreateDb::registerFactory( factory_function_type new_factory )
	{
		using namespace auto_get_or_create_db;
		factory= std::move( new_factory );
	}

	auto
	AutoGetOrCreateDb::makeImpl( OperationContext *const opCtx, const StringData ns, const LockMode mode )
			-> std::unique_ptr< Impl >
	{
		using namespace auto_get_or_create_db;
		return factory( opCtx, ns, mode );
	}



	AutoStatsTracker::Impl::~Impl()= default;

	void AutoStatsTracker::TUHook::hook() noexcept {}

	namespace
	{
		namespace auto_stats_tracker
		{
			AutoStatsTracker::factory_function_type factory= EvilAbort{};
		}//namespace auto_stats_tracker
	}//namespace

	void
	AutoStatsTracker::registerFactory( factory_function_type new_factory )
	{
		using namespace auto_stats_tracker;
		factory= std::move( new_factory );
	}

	auto
	AutoStatsTracker::makeImpl( OperationContext *const opCtx, const NamespaceString &nss, const Top::LockType lockType, const boost::optional< int > dbProfilingLevel )
			-> std::unique_ptr< Impl >
	{
		using namespace auto_stats_tracker;
		return factory( opCtx, nss, lockType, dbProfilingLevel );
	}




	AutoGetCollectionForRead::Impl::~Impl()= default;

	void AutoGetCollectionForRead::TUHook::hook() noexcept {}

	namespace
	{
		namespace auto_get_collection_for_read
		{
			AutoGetCollectionForRead::factory_function_type factory= EvilAbort{};
		}//namespace auto_get_collection_for_read
	}//namespace

	void
	AutoGetCollectionForRead::registerFactory( factory_function_type new_factory )
	{
		using namespace auto_get_collection_for_read;
		factory= std::move( new_factory );
	}

	auto
	AutoGetCollectionForRead::makeImpl( OperationContext *const opCtx, const NamespaceString &nss, const AutoGetCollection::ViewMode viewMode )
			-> std::unique_ptr< Impl >
	{
		using namespace auto_get_collection_for_read;
		return factory( opCtx, nss, viewMode );
	}



	AutoGetCollectionForReadCommand::Impl::~Impl()= default;

	void AutoGetCollectionForReadCommand::TUHook::hook() noexcept {}

	namespace
	{
		namespace auto_get_collection_for_read_command
		{
			AutoGetCollectionForReadCommand::factory_function_type factory= EvilAbort{};
		}//namespace auto_get_collection_for_read_command
	}//namespace

	void
	AutoGetCollectionForReadCommand::registerFactory( factory_function_type new_factory )
	{
		using namespace auto_get_collection_for_read_command;
		factory= std::move( new_factory );
	}

	auto
	AutoGetCollectionForReadCommand::makeImpl( OperationContext *const opCtx, const NamespaceString &nss, const AutoGetCollection::ViewMode viewMode )
			-> std::unique_ptr< Impl >
	{
		using namespace auto_get_collection_for_read_command;
		return factory( opCtx, nss, viewMode );
	}



	AutoGetCollectionOrViewForReadCommand::Impl::~Impl()= default;

	void AutoGetCollectionOrViewForReadCommand::TUHook::hook() noexcept {}

	namespace
	{
		namespace auto_get_collection_or_view_for_read_command
		{
			AutoGetCollectionOrViewForReadCommand::factory_function_type factory= EvilAbort{};
		}//namespace auto_get_collection_or_view_for_read_command
	}//namespace
	
	void
	AutoGetCollectionOrViewForReadCommand::registerFactory( factory_function_type new_factory )
	{
		using namespace auto_get_collection_or_view_for_read_command;
		factory= std::move( new_factory );
	}

	auto
	AutoGetCollectionOrViewForReadCommand::makeImpl( OperationContext *const opCtx, const NamespaceString &nss )
			-> std::unique_ptr< Impl >
	{
		using namespace auto_get_collection_or_view_for_read_command;
		return factory( opCtx, nss );
	}



	OldClientContext::Impl::~Impl()= default;

	void OldClientContext::TUHook::hook() noexcept {}

	namespace
	{
		namespace old_client_context
		{
			OldClientContext::factory_function_type factory= EvilAbort{};
		}//namespace old_client_context

		namespace old_client_context2
		{
			OldClientContext::factory_function_type2 factory= EvilAbort{};
		}//namespace old_client_context
	}//namespace

	void
	OldClientContext::registerFactory( factory_function_type new_factory )
	{
		using namespace old_client_context;
		factory= std::move( new_factory );
	}

	void
	OldClientContext::registerFactory( factory_function_type2 new_factory )
	{
		using namespace old_client_context2;
		factory= std::move( new_factory );
	}

	auto
	OldClientContext::makeImpl( OperationContext *const opCtx, const std::string &ns, const bool doVersion )
			-> std::unique_ptr< Impl >
	{
		using namespace old_client_context;
		return factory( opCtx, ns, doVersion );
	}

	auto
	OldClientContext::makeImpl2( OperationContext *const opCtx, const std::string &ns, Database *const db, const bool justCreated )
			-> std::unique_ptr< Impl >
	{
		using namespace old_client_context2;
		return factory( opCtx, ns, db, justCreated );
	}



	OldClientWriteContext::Impl::~Impl()= default;

	void OldClientWriteContext::TUHook::hook() noexcept {}

	namespace
	{
		namespace old_client_write_context
		{
			OldClientWriteContext::factory_function_type factory= EvilAbort{};
		}//namespace old_client_write_context
	}//namespace

	void
	OldClientWriteContext::registerFactory( factory_function_type new_factory )
	{
		using namespace old_client_write_context;
		factory= std::move( new_factory );
	}

	auto
	OldClientWriteContext::makeImpl( OperationContext *const opCtx, const std::string &ns )
			-> std::unique_ptr< Impl >
	{
		using namespace old_client_write_context;
		return factory( opCtx, ns );
	}
}//namespace mongo
