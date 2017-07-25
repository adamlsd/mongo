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

#include <string>

#include "mongo/base/string_data.h"
#include "mongo/db/catalog/database.h"
#include "mongo/db/catalog/database_holder.h"
#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/curop.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/stats/top.h"
#include "mongo/db/views/view.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/util/fail_point_service.h"
#include "mongo/util/timer.h"

namespace mongo
{
	namespace
	{
		namespace
		{
			MONGO_FP_DECLARE( setAutoGetCollectionWait );
		}  // namespace

		template< typename T >
		Status
		makeStatused( T callable )
		try
		{
			callable();
			return Status::OK();
		}
		catch( ... )
		{
			return exceptionToStatus();
		}

		template< typename T >
		auto
		factory()
		{
			return []( auto && ... args )
			{
				return stdx::make_unique< T >( args... );
			};
		}

		template< typename T >
		void
		registerFactory_impl()
		{
			T::facade_type::registerFactory( factory< T >() );
		}

		template< typename T >
		Status
		registerFactory()
		{
			return makeStatused( []{ registerFactory_impl< T >(); } );
		}

		namespace db_raii_impl
		{
			// To avoid name collision outside this namespace -- Windows compilers seem to do the wrong thing here.
			class AutoGetCollectionOrView;
			class AutoGetCollectionForRead;
			class AutoGetCollectionForReadCommand;
			class AutoGetCollectionOrViewForReadCommand;

			class AutoGetDb : public mongo::AutoGetDb::Impl
			{
				private:
					AutoGetDb( const AutoGetDb & )= delete;
					AutoGetDb &operator= ( const AutoGetDb & )= delete;

				public:
					explicit AutoGetDb( OperationContext *const opCtx, const StringData ns, const LockMode mode ) : _dbLock( opCtx, ns, mode ), _db( dbHolder().get( opCtx, ns ) ) {}

					Database *getDb() const final { return _db; }

				private:
					const Lock::DBLock _dbLock;
					Database *const _db;
			};

			MONGO_INITIALIZER( InitializeAutoGetDbFactory )( InitializerContext *const )
			{
				return registerFactory< AutoGetDb >();
			}


			class AutoGetCollection : public mongo::AutoGetCollection::Impl
			{
				private:
					using ViewMode= mongo::AutoGetCollection::Impl::ViewMode;

					AutoGetCollection( const AutoGetCollection & )= delete;
					AutoGetCollection &operator= ( const AutoGetCollection & )= delete;

				public:
					explicit AutoGetCollection( OperationContext *opCtx, const NamespaceString &nss, LockMode modeDB, LockMode modeColl, ViewMode viewMode );

					explicit inline AutoGetCollection( OperationContext *const opCtx, const NamespaceString &nss, const LockMode modeAll )
						: AutoGetCollection( opCtx, nss, modeAll, modeAll, ViewMode::kViewsForbidden ) {}

					Database *getDb() const final { return _autoDb.getDb(); }

					Collection *getCollection() const final { return _coll; }

				private:
					const ViewMode _viewMode;
					const mongo::AutoGetDb _autoDb;
					const Lock::CollectionLock _collLock;
					Collection *const _coll;

					friend class AutoGetCollectionOrView;
					friend class AutoGetCollectionForRead;
					friend class AutoGetCollectionForReadCommand;
					friend class AutoGetCollectionOrViewForReadCommand;
			};

			MONGO_INITIALIZER( InitializeAutoGetCollectionFactory )( InitializerContext *const )
			{
				return registerFactory< AutoGetCollection >();
			}



			class AutoGetCollectionOrView : public mongo::AutoGetCollectionOrView::Impl
			{
				private:
					AutoGetCollectionOrView( const AutoGetCollectionOrView & )= delete;
					AutoGetCollectionOrView &operator= ( const AutoGetCollectionOrView & )= delete;

				public:
					explicit AutoGetCollectionOrView( OperationContext *opCtx, const NamespaceString &nss, LockMode modeAll );

					Database *getDb() const final { return _autoColl.getDb(); }

					Collection *getCollection() const final { return _autoColl.getCollection(); }

					ViewDefinition *getView() const final { return _view.get(); }

				private:
					const AutoGetCollection _autoColl;
					std::shared_ptr< ViewDefinition > _view;
			};

			MONGO_INITIALIZER( InitializeAutoGetCollectionOrViewFactory )( InitializerContext *const )
			{
				return registerFactory< AutoGetCollectionOrView >();
			}



			class AutoGetOrCreateDb : public mongo::AutoGetOrCreateDb::Impl
			{
				AutoGetOrCreateDb( const AutoGetOrCreateDb & );
				AutoGetOrCreateDb &operator= ( const AutoGetOrCreateDb & );

				public:
					explicit AutoGetOrCreateDb( OperationContext *opCtx, StringData ns, LockMode mode );

					Database *getDb() const final { return _db; }

					bool justCreated() const final { return _justCreated; }

					Lock::DBLock &lock() final { return _dbLock; }

				private:
					Lock::DBLock _dbLock;  // not const, as we may need to relock for implicit create
					Database *_db;
					bool _justCreated;
			};

			MONGO_INITIALIZER( InitializeAutoGetOrCreateDbFactory )( InitializerContext *const )
			{
				return registerFactory< AutoGetOrCreateDb >();
			}



			class AutoStatsTracker : public mongo::AutoStatsTracker::Impl
			{
				private:
					AutoStatsTracker( const AutoStatsTracker & )= delete;
					AutoStatsTracker &operator= ( const AutoStatsTracker & )= delete;

				public:
					explicit AutoStatsTracker( OperationContext *opCtx, const NamespaceString &nss, Top::LockType lockType, boost::optional< int > dbProfilingLevel );

					~AutoStatsTracker() final;

				private:
					OperationContext *_opCtx;
					Top::LockType _lockType;
			};

			MONGO_INITIALIZER( InitializeAutoStatsTrackerFactory )( InitializerContext *const )
			{
				return registerFactory< AutoStatsTracker >();
			}



			class AutoGetCollectionForRead : public mongo::AutoGetCollectionForRead::Impl
			{
				AutoGetCollectionForRead( const AutoGetCollectionForRead & );
				AutoGetCollectionForRead &operator= ( const AutoGetCollectionForRead & );

				public:
					explicit AutoGetCollectionForRead( OperationContext *opCtx, const NamespaceString &nss, AutoGetCollection::ViewMode viewMode );

					Database *getDb() const final { return _autoColl->getDb(); }

					Collection *getCollection() const final { return _autoColl->getCollection(); }

				private:
					void _ensureMajorityCommittedSnapshotIsValid( const NamespaceString &nss, OperationContext *opCtx );

					boost::optional< AutoGetCollection > _autoColl;
			};

			MONGO_INITIALIZER( InitializeAutoGetCollectionForReadFactory )( InitializerContext *const )
			{
				return registerFactory< AutoGetCollectionForRead >();
			}



			class AutoGetCollectionForReadCommand : public virtual mongo::AutoGetCollectionForReadCommand::Impl
			{
				private:
					AutoGetCollectionForReadCommand( const AutoGetCollectionForReadCommand & )= delete;
					AutoGetCollectionForReadCommand &operator= ( const AutoGetCollectionForReadCommand & )= delete;

				public:
					Database *getDb() const final { return _autoCollForRead->getDb(); }

					Collection *getCollection() const final { return _autoCollForRead->getCollection(); }

					explicit AutoGetCollectionForReadCommand( OperationContext *opCtx, const NamespaceString &nss, AutoGetCollection::ViewMode viewMode );

				protected:
					// '_autoCollForRead' may need to be reset by AutoGetCollectionOrViewForReadCommand, so needs to
					// be a boost::optional.
					boost::optional< AutoGetCollectionForRead > _autoCollForRead;

					// This needs to be initialized after 'autoCollForRead', since we need to consult the Database
					// object to get the profiling level. Thus, it needs to be a boost::optional.
					boost::optional< mongo::AutoStatsTracker > _statsTracker;
			};

			MONGO_INITIALIZER( InitializeAutoGetCollectionForReadCommandFactory )( InitializerContext *const )
			{
				return registerFactory< AutoGetCollectionForReadCommand >();
			}



			class AutoGetCollectionOrViewForReadCommand final
					: virtual public AutoGetCollectionForReadCommand, virtual public mongo::AutoGetCollectionOrViewForReadCommand::Impl
			{
				private:
					AutoGetCollectionOrViewForReadCommand( const AutoGetCollectionOrViewForReadCommand & )= delete;
					AutoGetCollectionOrViewForReadCommand &operator= ( const AutoGetCollectionOrViewForReadCommand & )= delete;

				public:
					explicit AutoGetCollectionOrViewForReadCommand( OperationContext *opCtx, const NamespaceString &nss );

					ViewDefinition *getView() const final { return _view.get(); }

					void releaseLocksForView() noexcept final;

				private:
					std::shared_ptr< ViewDefinition > _view;
			};

			MONGO_INITIALIZER( InitializeAutoGetCollectionOrViewForReadCommandFactory )( InitializerContext *const )
			{
				return registerFactory< AutoGetCollectionOrViewForReadCommand >();
			}




			class OldClientContext : public mongo::OldClientContext::Impl
			{
				private:
					OldClientContext( const OldClientContext & )= delete;
					OldClientContext &operator= ( const OldClientContext & )= delete;

				public:
					explicit OldClientContext( OperationContext *opCtx, const std::string &ns, bool doVersion );

					explicit OldClientContext( OperationContext *opCtx, const std::string &ns, Database *db, bool justCreated );

					~OldClientContext() final;

					Database *db() const final { return _db; }

					bool justCreated() const final { return _justCreated; }

				private:
					friend class CurOp;
					void _finishInit();
					void _checkNotStale() const;

					bool _justCreated;
					bool _doVersion;
					const std::string _ns;
					Database *_db;
					OperationContext *_opCtx;

					Timer _timer;
			};

			MONGO_INITIALIZER( InitializeOldClientContextFactory )( InitializerContext *const )
			{
				return makeStatused( []
						{
							using factory_function_type= OldClientContext::facade_type::factory_function_type;
							OldClientContext::facade_type::registerFactory( factory_function_type( factory< OldClientContext >() ) );
							using factory_function_type2= OldClientContext::facade_type::factory_function_type2;
							OldClientContext::facade_type::registerFactory( factory_function_type2( factory< OldClientContext >() ) );
						} );
			}



			class OldClientWriteContext : public mongo ::OldClientWriteContext::Impl
			{
				private:
					OldClientWriteContext( const OldClientWriteContext & );
					OldClientWriteContext &operator= ( const OldClientWriteContext & );

				public:
					explicit OldClientWriteContext( OperationContext *opCtx, const std::string &ns );

					Database *db() const final { return _c.db(); }

					Collection *getCollection() const final { return _c.db()->getCollection( _opCtx, _nss ); }

				private:
					OperationContext *const _opCtx;
					const NamespaceString _nss;

					mongo::AutoGetOrCreateDb _autodb;
					Lock::CollectionLock _collk;
					OldClientContext _c;
					Collection *_collection;
			};

			MONGO_INITIALIZER( InitializeOldClientWriteContextFactory )( InitializerContext *const )
			{
				return registerFactory< OldClientWriteContext >();
			}



			AutoGetCollection::AutoGetCollection( OperationContext *opCtx,
					const NamespaceString &nss,
					LockMode modeDB,
					LockMode modeColl,
					ViewMode viewMode )
				: _viewMode( viewMode ),
				_autoDb( opCtx, nss.db(), modeDB ),
				_collLock( opCtx->lockState(), nss.ns(), modeColl ),
				_coll( _autoDb.getDb() ? _autoDb.getDb()->getCollection( opCtx, nss ) : nullptr )
			{
				Database *db= _autoDb.getDb();

				// If the database exists, but not the collection, check for views.
				if( _viewMode == ViewMode::kViewsForbidden && db && !_coll &&
						db->getViewCatalog()->lookup( opCtx, nss.ns() ) ) uasserted( ErrorCodes::CommandNotSupportedOnView,
							str::stream() << "Namespace " << nss.ns() << " is a view, not a collection" );

				// Wait for a configured amount of time after acquiring locks if the failpoint is enabled.
				MONGO_FAIL_POINT_BLOCK( setAutoGetCollectionWait, customWait )
				{
					const BSONObj &data= customWait.getData();
					sleepFor( Milliseconds( data[ "waitForMillis" ].numberInt() ) );
				}
			}

			AutoGetCollectionOrView::AutoGetCollectionOrView( OperationContext *opCtx, const NamespaceString &nss, LockMode modeAll )
				: _autoColl( opCtx, nss, modeAll, modeAll, AutoGetCollection::ViewMode::kViewsPermitted ),
				_view( _autoColl.getDb() && !_autoColl.getCollection() ? _autoColl.getDb()->getViewCatalog()->lookup( opCtx, nss.ns() ) : nullptr ) {}

			AutoGetOrCreateDb::AutoGetOrCreateDb( OperationContext *opCtx, StringData ns, LockMode mode )
				: _dbLock( opCtx, ns, mode ), _db( dbHolder().get( opCtx, ns ) )
			{
				invariant( mode == MODE_IX || mode == MODE_X );
				_justCreated= false;

				// If the database didn't exist, relock in MODE_X
				if( _db == nullptr )
				{
					if( mode != MODE_X )
					{
						_dbLock.relockWithMode( MODE_X );
					}
					_db= dbHolder().openDb( opCtx, ns );
					_justCreated= true;
				}
			}

			AutoStatsTracker::AutoStatsTracker( OperationContext *opCtx, const NamespaceString &nss, Top::LockType lockType, boost::optional< int > dbProfilingLevel )
				: _opCtx( opCtx ), _lockType( lockType )
			{
				if( !dbProfilingLevel )
				{
					// No profiling level was determined, attempt to read the profiling level from the Database
					// object.
					AutoGetDb autoDb( _opCtx, nss.db(), MODE_IS );

					if( autoDb.getDb() )
					{
						dbProfilingLevel= autoDb.getDb()->getProfilingLevel();
					}
				}
				stdx::lock_guard< Client > clientLock( *_opCtx->getClient() );
				CurOp::get( _opCtx )->enter_inlock( nss.ns().c_str(), dbProfilingLevel );
			}

			AutoStatsTracker::~AutoStatsTracker()
			{
				auto curOp= CurOp::get( _opCtx );
				Top::get( _opCtx->getServiceContext() ).record( _opCtx, curOp->getNS(), curOp->getLogicalOp(), _lockType, durationCount< Microseconds >( curOp->elapsedTimeExcludingPauses() ),
						curOp->isCommand(), curOp->getReadWriteType() );
			}

			AutoGetCollectionForRead::AutoGetCollectionForRead( OperationContext *opCtx, const NamespaceString &nss, AutoGetCollection::ViewMode viewMode )
			{
				_autoColl.emplace( opCtx, nss, MODE_IS, MODE_IS, viewMode );

				// Note: this can yield.
				_ensureMajorityCommittedSnapshotIsValid( nss, opCtx );
			}

			void
			AutoGetCollectionForRead::_ensureMajorityCommittedSnapshotIsValid( const NamespaceString &nss, OperationContext *opCtx )
			{
				while( true )
				{
					auto coll= _autoColl->getCollection();

					if( !coll ) { return; }
					auto minSnapshot= coll->getMinimumVisibleSnapshot();

					if( !minSnapshot ) { return; }
					auto mySnapshot= opCtx->recoveryUnit()->getMajorityCommittedSnapshot();

					if( !mySnapshot ) { return; }

					if( mySnapshot >= minSnapshot ) { return; }

					// Yield locks.
					_autoColl= boost::none;

					repl::ReplicationCoordinator::get( opCtx )->waitUntilSnapshotCommitted( opCtx, *minSnapshot );

					uassertStatusOK( opCtx->recoveryUnit()->setReadFromMajorityCommittedSnapshot() );

					{
						stdx::lock_guard< Client > lk( *opCtx->getClient() );
						CurOp::get( opCtx )->yielded();
					}

					// Relock.
					_autoColl.emplace( opCtx, nss, MODE_IS );
				}
			}

			AutoGetCollectionForReadCommand::AutoGetCollectionForReadCommand( OperationContext *opCtx, const NamespaceString &nss, AutoGetCollection::ViewMode viewMode )
			{
				_autoCollForRead.emplace( opCtx, nss, viewMode );
				const int doNotChangeProfilingLevel= 0;
				_statsTracker.emplace( opCtx, nss, Top::LockType::ReadLocked, _autoCollForRead->getDb() ? _autoCollForRead->getDb()->getProfilingLevel() : doNotChangeProfilingLevel );

				// We have both the DB and collection locked, which is the prerequisite to do a stable shard
				// version check, but we'd like to do the check after we have a satisfactory snapshot.
				auto css= CollectionShardingState::get( opCtx, nss );
				css->checkShardVersionOrThrow( opCtx );
			}

			AutoGetCollectionOrViewForReadCommand::AutoGetCollectionOrViewForReadCommand( OperationContext *opCtx, const NamespaceString &nss )
				: AutoGetCollectionForReadCommand( opCtx, nss, AutoGetCollection::ViewMode::kViewsPermitted ),
				_view( _autoCollForRead->getDb() && !getCollection() ? _autoCollForRead->getDb()->getViewCatalog()->lookup( opCtx, nss.ns() ) : nullptr ) {}

			void
			AutoGetCollectionOrViewForReadCommand::releaseLocksForView() noexcept
			{
				invariant( _view );
				_view= nullptr;
				_autoCollForRead= boost::none;
			}

			OldClientContext::OldClientContext( OperationContext *opCtx, const std::string &ns, Database *db, bool justCreated )
				: _justCreated( justCreated ), _doVersion( true ), _ns( ns ), _db( db ), _opCtx( opCtx ) { _finishInit(); }

			OldClientContext::OldClientContext( OperationContext *opCtx, const std::string &ns, bool doVersion )
				: _justCreated( false ),  // set for real in finishInit
				_doVersion( doVersion ), _ns( ns ), _db( NULL ), _opCtx( opCtx )
			{
				_finishInit();
			}

			void
			OldClientContext::_finishInit()
			{
				_db= dbHolder().get( _opCtx, _ns );

				if( _db ) { _justCreated= false; }
				else
				{
					invariant( _opCtx->lockState()->isDbLockedForMode( nsToDatabaseSubstring( _ns ), MODE_X ) );
					_db= dbHolder().openDb( _opCtx, _ns, &_justCreated );
					invariant( _db );
				}

				if( _doVersion ) { _checkNotStale(); }

				stdx::lock_guard< Client > lk( *_opCtx->getClient() );
				CurOp::get( _opCtx )->enter_inlock( _ns.c_str(), _db->getProfilingLevel() );
			}

			void
			OldClientContext::_checkNotStale() const
			{
				switch( CurOp::get( _opCtx )->getNetworkOp() )
				{
					case dbGetMore:  // getMore is special and should be handled elsewhere.
					case dbUpdate:   // update & delete check shard version in instance.cpp, so don't check
					case dbDelete:   // here as well.
						break;

					default:
						auto css= CollectionShardingState::get( _opCtx, _ns );
						css->checkShardVersionOrThrow( _opCtx );
				}
			}

			OldClientContext::~OldClientContext()
			{
				// Lock must still be held
				invariant( _opCtx->lockState()->isLocked() );

				auto currentOp= CurOp::get( _opCtx );
				Top::get( _opCtx->getClient()->getServiceContext() ) .record( _opCtx, currentOp->getNS(), currentOp->getLogicalOp(), _opCtx->lockState()->isWriteLocked()
						? Top::LockType::WriteLocked
						: Top::LockType::ReadLocked,
						_timer.micros(), currentOp->isCommand(), currentOp->getReadWriteType() );
			}

			OldClientWriteContext::OldClientWriteContext( OperationContext *opCtx, const std::string &ns )
				: _opCtx( opCtx ), _nss( ns ), _autodb( opCtx, _nss.db(), MODE_IX ), _collk( opCtx->lockState(), ns, MODE_IX ), _c( opCtx, ns, _autodb.getDb(), _autodb.justCreated() )
			{
				_collection= _c.db()->getCollection( opCtx, ns );

				if( !_collection && !_autodb.justCreated() )
				{
					// relock database in MODE_X to allow collection creation
					_collk.relockAsDatabaseExclusive( _autodb.lock() );
					Database *db= dbHolder().get( _opCtx, ns );
					invariant( db == _c.db() );
				}
			}
		}  // namespace db_raii_impl
	} //namespace
}//namespace mongo
