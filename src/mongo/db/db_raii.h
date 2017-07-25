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

#pragma once

#include <string>

#include "mongo/base/string_data.h"
#include "mongo/db/catalog/collection.h"
#include "mongo/db/catalog/database.h"
#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/stats/top.h"
#include "mongo/db/views/view.h"
#include "mongo/util/timer.h"

namespace mongo
{
    /**
     * RAII-style class, which acquires a lock on the specified database in the requested mode and
     * obtains a reference to the database. Used as a shortcut for calls to dbHolder().get().
     *
     * Use this when you want to do a database-level operation, like read a list of all collections, or
     * drop a collection.
     *
     * It is guaranteed that the lock will be released when this object goes out of scope, therefore
     * the database reference returned by this class should not be retained.
     */
    class AutoGetDb
    {
		public:
			class Impl
			{
				public:
					virtual ~Impl()= 0;

					virtual Database *getDb() const= 0;
			};

		private:
			std::unique_ptr< Impl > _pimpl;

			static std::unique_ptr< Impl > makeImpl( OperationContext *opCtx, StringData ns, LockMode mode );

			struct TUHook
			{
				static void hook() noexcept;
				explicit inline TUHook() noexcept { if( kDebugBuild ) this->hook(); }
			};

			inline Impl &_impl() { TUHook{}; return *this->_pimpl; }

			inline const Impl &_impl() const { TUHook{}; return *this->_pimpl; }

        public:
			using factory_function_type= stdx::function< decltype( makeImpl ) >;
			
			static void registerFactory( factory_function_type factory );

			inline ~AutoGetDb()= default;

			explicit inline
			AutoGetDb( OperationContext *const opCtx, const StringData ns, const LockMode mode )
				: _pimpl( makeImpl( opCtx, ns, mode ) ) {}

            inline Database *getDb() const { return this->_impl().getDb(); }
    };

    /**
     * RAII-style class, which acquires a locks on the specified database and collection in the
     * requested mode and obtains references to both.
     *
     * Use this when you want to access something at the collection level, but do not want to do any of
     * the tasks associated with the 'ForRead' variants below. For example, you can use this to access a
     * Collection's CursorManager, or to remove a document.
     *
     * It is guaranteed that locks will be released when this object goes out of scope, therefore
     * the database and the collection references returned by this class should not be retained.
     */
    class AutoGetCollection
    {
		private:
			enum class ViewMode { kViewsPermitted, kViewsForbidden };

			friend class AutoGetCollectionOrView;
			friend class AutoGetCollectionForRead;
			friend class AutoGetCollectionForReadCommand;
			friend class AutoGetCollectionOrViewForReadCommand;

        public:
			class Impl
			{
				protected:
					using ViewMode= AutoGetCollection::ViewMode;

				public:
					virtual ~Impl()= 0;

					virtual Database *getDb() const= 0;

					virtual Collection *getCollection() const= 0;
			};

		private:
			std::unique_ptr< Impl > _pimpl;

			static std::unique_ptr< Impl > makeImpl( OperationContext *opCtx, const NamespaceString &nss, LockMode modeDB, LockMode modeColl, ViewMode viewMode );

			struct TUHook
			{
				static void hook() noexcept;
				explicit inline TUHook() noexcept { if( kDebugBuild ) this->hook(); }
			};

			inline Impl &_impl() { TUHook{}; return *this->_pimpl; }

			inline const Impl &_impl() const { TUHook{}; return *this->_pimpl; }

        public:
			using factory_function_type= stdx::function< decltype( makeImpl ) >;
			
			static void registerFactory( factory_function_type factory );

			inline ~AutoGetCollection()= default;

		private:
            /**
             * This constructor is intended for internal use and should not be used outside this file.
             * AutoGetCollectionForReadCommand and AutoGetCollectionOrViewForReadCommand use 'viewMode' to
             * determine whether or not it is permissible to obtain a handle on a view namespace. Use
             * another constructor or another 'AutoGet' class instead.
             */
            explicit inline AutoGetCollection( OperationContext *const opCtx, const NamespaceString &nss, const LockMode modeDB, const LockMode modeColl, const ViewMode viewMode )
				: _pimpl( makeImpl( opCtx, nss, modeDB, modeColl, viewMode ) ) {}

		public:
            inline AutoGetCollection( OperationContext *const opCtx, const NamespaceString &nss, const LockMode modeAll )
                : AutoGetCollection( opCtx, nss, modeAll, modeAll, ViewMode::kViewsForbidden ) {}

            inline AutoGetCollection( OperationContext *opCtx,
                    const NamespaceString &nss,
                    const LockMode modeDB,
                    const LockMode modeColl )
                : AutoGetCollection( opCtx, nss, modeDB, modeColl, ViewMode::kViewsForbidden ) {}

            /**
             * Returns nullptr if the database didn't exist.
             */
            inline Database *getDb() const { return this->_impl().getDb(); }

            /**
             * Returns nullptr if the collection didn't exist.
             */
            inline Collection *getCollection() const { return this->_impl().getCollection(); }
    };

    /**
     * RAII-style class which acquires the appropriate hierarchy of locks for a collection or
     * view. The pointer to a view definition is nullptr if it does not exist.
     *
     * Use this when you have not yet determined if the namespace is a view or a collection.
     * For example, you can use this to access a namespace's CursorManager.
     *
     * It is guaranteed that locks will be released when this object goes out of scope, therefore
     * the view returned by this class should not be retained.
     */
    class AutoGetCollectionOrView
    {
        public:
			class Impl
			{
				public:
					virtual ~Impl()= 0;

					virtual Database *getDb() const= 0;

					virtual Collection *getCollection() const= 0;

					virtual ViewDefinition *getView() const= 0;
			};

		private:
			std::unique_ptr< Impl > _pimpl;

			static std::unique_ptr< Impl > makeImpl( OperationContext *opCtx, const NamespaceString &nss, LockMode modeAll );

			struct TUHook
			{
				static void hook() noexcept;
				explicit inline TUHook() noexcept { if( kDebugBuild ) this->hook(); }
			};

			inline Impl &_impl() { TUHook{}; return *this->_pimpl; }

			inline const Impl &_impl() const { TUHook{}; return *this->_pimpl; }


		public:
			using factory_function_type= stdx::function< decltype( makeImpl ) >;
			
			static void registerFactory( factory_function_type factory );

			inline ~AutoGetCollectionOrView()= default;

            explicit inline
			AutoGetCollectionOrView( OperationContext *const opCtx, const NamespaceString &nss, const LockMode modeAll )
					: _pimpl( makeImpl( opCtx, nss, modeAll ) ) {}

            /**
             * Returns nullptr if the database didn't exist.
             */
            inline Database *getDb() const { return this->_impl().getDb(); }

            /**
             * Returns nullptr if the collection didn't exist.
             */
            inline Collection *getCollection() const { return this->_impl().getCollection(); }

            /**
             * Returns nullptr if the view didn't exist.
             */
            inline ViewDefinition *getView() const { return this->_impl().getView(); }
    };

    /**
     * RAII-style class, which acquires a lock on the specified database in the requested mode and
     * obtains a reference to the database, creating it was non-existing. Used as a shortcut for
     * calls to dbHolder().openDb(), taking care of locking details. The requested mode must be
     * MODE_IX or MODE_X. If the database needs to be created, the lock will automatically be
     * reacquired as MODE_X.
     *
     * Use this when you are about to perform a write, and want to create the database if it doesn't
     * already exist.
     *
     * It is guaranteed that locks will be released when this object goes out of scope, therefore
     * the database reference returned by this class should not be retained.
     */
    class AutoGetOrCreateDb
    {
        public:
			class Impl
			{
				public:
					virtual ~Impl()= 0;

					virtual Database *getDb() const= 0;
					virtual bool justCreated() const= 0;

					virtual Lock::DBLock &lock()= 0;
			};

		private:
			std::unique_ptr< Impl > _pimpl;

			static std::unique_ptr< Impl > makeImpl( OperationContext *opCtx, StringData ns, LockMode mode );

			struct TUHook
			{
				static void hook() noexcept;
				explicit inline TUHook() noexcept { if( kDebugBuild ) this->hook(); }
			};

			inline Impl &_impl() { TUHook{}; return *this->_pimpl; }

			inline const Impl &_impl() const { TUHook{}; return *this->_pimpl; }

		public:
			using factory_function_type= stdx::function< decltype( makeImpl ) >;
			
			static void registerFactory( factory_function_type factory );

            inline ~AutoGetOrCreateDb()= default;

            explicit inline
			AutoGetOrCreateDb( OperationContext *const opCtx, const StringData ns, const LockMode mode )
					: _pimpl( makeImpl( opCtx, ns, mode ) ) {}

            inline Database *getDb() const { return this->_impl().getDb(); }

            inline bool justCreated() const { return this->_impl().justCreated(); }

            inline Lock::DBLock &lock() { return this->_impl().lock(); }
    };

    /**
     * RAII-style class which automatically tracks the operation namespace in CurrentOp and records the
     * operation via Top upon destruction.
     */
    class AutoStatsTracker
    {
        public:
			class Impl
			{
				public:
					virtual ~Impl()= 0;
			};

		private:
			std::unique_ptr< Impl > _pimpl;

			static std::unique_ptr< Impl > makeImpl( OperationContext *opCtx, const NamespaceString &nss, Top::LockType lockType, boost::optional< int > dbProfilingLevel );

			struct TUHook
			{
				static void hook() noexcept;
				explicit inline TUHook() noexcept { if( kDebugBuild ) this->hook(); }
			};

			inline Impl &_impl() { TUHook{}; return *this->_pimpl; }

			inline const Impl &_impl() const { TUHook{}; return *this->_pimpl; }

		public:
			using factory_function_type= stdx::function< decltype( makeImpl ) >;
			
			static void registerFactory( factory_function_type factory );

			inline ~AutoStatsTracker()= default;
			
            /**
             * Sets the namespace of the CurOp object associated with 'opCtx' to be 'nss' and starts the
             * CurOp timer. 'lockType' describes which type of lock is held by this operation, and will be
             * used for reporting via Top. If 'dbProfilingLevel' is not given, this constructor will acquire
             * and then drop a database lock in order to determine the database's profiling level.
             */
			explicit inline
            AutoStatsTracker( OperationContext *const opCtx, const NamespaceString &nss, const Top::LockType lockType, const boost::optional< int > dbProfilingLevel )
					: _pimpl( makeImpl( opCtx, nss, lockType, dbProfilingLevel ) ) {}
    };

    /**
     * RAII-style class, which would acquire the appropriate hierarchy of locks for obtaining
     * a particular collection and would retrieve a reference to the collection. In addition, this
     * utility will ensure that the read will be performed against an appropriately committed snapshot
     * if the operation is using a readConcern of 'majority'.
     *
     * Use this when you want to read the contents of a collection, but you are not at the top-level of
     * some command. This will ensure your reads obey any requested readConcern, but will not update the
     * status of CurrentOp, or add a Top entry.
     *
     * It is guaranteed that locks will be released when this object goes out of scope, therefore
     * database and collection references returned by this class should not be retained.
     */
    class AutoGetCollectionForRead
    {
        public:
			class Impl
			{
				public:
					virtual ~Impl()= 0;

					virtual Database *getDb() const= 0;

					virtual Collection *getCollection() const= 0;
			};

		private:
			std::unique_ptr< Impl > _pimpl;

			static std::unique_ptr< Impl > makeImpl( OperationContext *opCtx, const NamespaceString &nss, AutoGetCollection::ViewMode viewMode );

			struct TUHook
			{
				static void hook() noexcept;
				explicit inline TUHook() noexcept { if( kDebugBuild ) this->hook(); }
			};

			inline Impl &_impl() { TUHook{}; return *this->_pimpl; }

			inline const Impl &_impl() const { TUHook{}; return *this->_pimpl; }

		public:
			using factory_function_type= stdx::function< decltype( makeImpl ) >;
			
			static void registerFactory( factory_function_type factory );

			inline ~AutoGetCollectionForRead()= default;

            explicit inline
			AutoGetCollectionForRead( OperationContext *const opCtx, const NamespaceString &nss )
                : AutoGetCollectionForRead( opCtx, nss, AutoGetCollection::ViewMode::kViewsForbidden ) {}

		private:
            /**
             * This constructor is intended for internal use and should not be used outside this file.
             * AutoGetCollectionForReadCommand and AutoGetCollectionOrViewForReadCommand use 'viewMode' to
             * determine whether or not it is permissible to obtain a handle on a view namespace. Use
             * another constructor or another 'AutoGet' class instead.
             */
			explicit inline
            AutoGetCollectionForRead( OperationContext *const opCtx, const NamespaceString &nss, const AutoGetCollection::ViewMode viewMode )
					: _pimpl( makeImpl( opCtx, nss, viewMode ) ) {}

		public:
            inline Database *getDb() const { return this->_impl().getDb(); }

            inline Collection *getCollection() const { return this->_impl().getCollection(); }
    };

    /**
     * RAII-style class, which would acquire the appropriate hierarchy of locks for obtaining
     * a particular collection and would retrieve a reference to the collection. In addition, this
     * utility validates the shard version for the specified namespace and sets the current operation's
     * namespace for the duration while this object is alive.
     *
     * Use this when you are a read-only command and you know that your target namespace is a collection
     * (not a view). In addition to ensuring your read obeys any requested readConcern, this will add a
     * Top entry upon destruction and ensure the CurrentOp object has the right namespace and has
     * started its timer.
     *
     * It is guaranteed that locks will be released when this object goes out of scope, therefore
     * database and collection references returned by this class should not be retained.
     */
    class AutoGetCollectionForReadCommand
    {
        public:
			class Impl
			{
				public:
					virtual ~Impl()= 0;

					virtual Database *getDb() const= 0;

					virtual Collection *getCollection() const= 0;
			};

		private:
			std::unique_ptr< Impl > _pimpl;

			static std::unique_ptr< Impl > makeImpl( OperationContext *opCtx, const NamespaceString &nss, AutoGetCollection::ViewMode viewMode );

			struct TUHook
			{
				static void hook() noexcept;
				explicit inline TUHook() noexcept { if( kDebugBuild ) this->hook(); }
			};

			inline Impl &_impl() { TUHook{}; return *this->_pimpl; }

			inline const Impl &_impl() const { TUHook{}; return *this->_pimpl; }

		public:
			using factory_function_type= stdx::function< decltype( makeImpl ) >;
			
			static void registerFactory( factory_function_type factory );

            inline ~AutoGetCollectionForReadCommand()= default;

			explicit inline
            AutoGetCollectionForReadCommand( OperationContext *const opCtx, const NamespaceString &nss )
                : AutoGetCollectionForReadCommand( opCtx, nss, AutoGetCollection::ViewMode::kViewsForbidden ) {}

            inline Database *getDb() const { return this->_impl().getDb(); }

            inline Collection *getCollection() const { return this->_impl().getCollection(); }

        private:
            explicit inline AutoGetCollectionForReadCommand( OperationContext *const opCtx, const NamespaceString &nss, const AutoGetCollection::ViewMode viewMode )
					: _pimpl( makeImpl( opCtx, nss, viewMode ) ) {}

		protected:
			explicit inline AutoGetCollectionForReadCommand( std::unique_ptr< Impl > pimpl ) : _pimpl( std::move( pimpl ) ) {}
    };

    /**
     * RAII-style class for obtaining a collection or view for reading. The pointer to a view definition
     * is nullptr if it does not exist.
     *
     * Use this when you are a read-only command, but have not yet determined if the namespace is a view
     * or a collection.
     */
    class AutoGetCollectionOrViewForReadCommand final
            : public AutoGetCollectionForReadCommand
    {
        public:
			class Impl : virtual public AutoGetCollectionForReadCommand::Impl
			{
				public:
					virtual ~Impl()= 0;

					virtual ViewDefinition *getView() const= 0;
					virtual void releaseLocksForView() noexcept= 0;
			};


		private:
			Impl *const _pimpl;

			static std::unique_ptr< Impl > makeImpl( OperationContext *opCtx, const NamespaceString &nss );

			struct TUHook
			{
				static void hook() noexcept;
				explicit inline TUHook() noexcept { if( kDebugBuild ) this->hook(); }
			};

			inline Impl &_impl() { TUHook{}; return *this->_pimpl; }

			inline const Impl &_impl() const { TUHook{}; return *this->_pimpl; }

		public:
			using factory_function_type= stdx::function< decltype( makeImpl ) >;
			
			static void registerFactory( factory_function_type factory );

			inline ~AutoGetCollectionOrViewForReadCommand()= default;

			explicit inline
            AutoGetCollectionOrViewForReadCommand( OperationContext *const opCtx, const NamespaceString &nss )
					: AutoGetCollectionOrViewForReadCommand( makeImpl( opCtx, nss ) ) {}

            inline ViewDefinition *getView() const { return this->_impl().getView(); }

            /**
             * Unlock this view or collection and release all resources. After calling this function, it is
             * illegal to access this object's database, collection and view pointers.
             *
             * TODO(SERVER-24909): Consider having the constructor release locks instead, or otherwise
             * remove the need for this method.
             */
            inline void releaseLocksForView() noexcept { return this->_impl().releaseLocksForView(); }

		private:
			// The reason for this chained, two-phase construction which takes unique_ptr by reference
			// in the second stage is to facilitate the derived class (this class) holding a pointer to
			// the derived class's pimpl form, while giving a "reduced" form to the parent.
			// This facilitates and preserves the substitutibility aspects of inheritance while still also
			// preserving the code-reuse portion.  It is conceivable that the author intended both, in the
			// original class's requirement for inheritance.

			explicit inline
			AutoGetCollectionOrViewForReadCommand( std::unique_ptr< Impl > pimpl )
					: AutoGetCollectionOrViewForReadCommand( pimpl, pimpl.get() ) {}

			// This constructor must take `pimpl` by reference, not instance, such that the constructor which forwards to it (above)
			// can create a raw pointer to the object being managed.  This raw pointer is required before the pimpl pointer is given
			// to the parent.  When the parent receives the pimpl ptr, it will keep it private, and view it as the base of the `Impl`
			// class (the `Impl` class of the parent).  To avoid a dynamic cast, we capture a raw C++ pointer before the scope of
			// `pimpl` is terminated by a move.  To do that, we must NOT move it from the previous stage constructor, otherwise
			// the `.get()` may be called on the stale `unique_ptr`, post move.
			explicit inline
			AutoGetCollectionOrViewForReadCommand( std::unique_ptr< Impl > &pimpl, Impl *const pimpl_raw )
					: AutoGetCollectionForReadCommand( std::move( pimpl )), _pimpl( pimpl_raw ) {}
    };

    /**
     * Opens the database that we want to use and sets the appropriate namespace on the
     * current operation.
     */
    class OldClientContext
    {
        public:
			class Impl
			{
				public:
					virtual ~Impl()= 0;

					virtual Database *db() const= 0;

					virtual bool justCreated() const= 0;
			};

		private:
			std::unique_ptr< Impl > _pimpl;

			static std::unique_ptr< Impl > makeImpl( OperationContext *opCtx, const std::string &ns, bool doVersion );
			static std::unique_ptr< Impl > makeImpl2( OperationContext *opCtx, const std::string &ns, Database *db, bool justCreated );

			struct TUHook
			{
				static void hook() noexcept;
				explicit inline TUHook() noexcept { if( kDebugBuild ) this->hook(); }
			};

			inline Impl &_impl() { TUHook{}; return *this->_pimpl; }

			inline const Impl &_impl() const { TUHook{}; return *this->_pimpl; }

		public:
			using factory_function_type= stdx::function< decltype( makeImpl ) >;
			using factory_function_type2= stdx::function< decltype( makeImpl2 ) >;
			
			static void registerFactory( factory_function_type factory );
			static void registerFactory( factory_function_type2 factory );

			inline ~OldClientContext()= default;

            /** this is probably what you want */
			explicit inline
            OldClientContext( OperationContext *const opCtx, const std::string &ns, const bool doVersion= true )
					: _pimpl( makeImpl( opCtx, ns, doVersion ) ) {}

            /**
             * Below still calls _finishInit, but assumes database has already been acquired
             * or just created.
             */
			explicit inline
            OldClientContext( OperationContext *const opCtx, const std::string &ns, Database *const db, const bool justCreated )
					: _pimpl( makeImpl2( opCtx, ns, db, justCreated ) ) {}


            inline Database *db() const { return this->_impl().db(); }

            /** @return if the db was created by this OldClientContext */
            inline bool justCreated() const { return this->_impl().justCreated(); }
    };


    class OldClientWriteContext
    {
        public:
			class Impl
			{
				public:
					virtual ~Impl()= 0;

					virtual Database *db() const= 0;

					virtual Collection *getCollection() const= 0;
            };
		private:
			std::unique_ptr< Impl > _pimpl;

			static std::unique_ptr< Impl > makeImpl( OperationContext *opCtx, const std::string &ns );

			struct TUHook
			{
				static void hook() noexcept;
				explicit inline TUHook() noexcept { if( kDebugBuild ) this->hook(); }
			};

			inline Impl &_impl() { TUHook{}; return *this->_pimpl; }

			inline const Impl &_impl() const { TUHook{}; return *this->_pimpl; }

		public:
			using factory_function_type= stdx::function< decltype( makeImpl ) >;
			
			static void registerFactory( factory_function_type factory );

			inline ~OldClientWriteContext()= default;

			explicit inline
            OldClientWriteContext( OperationContext *const opCtx, const std::string &ns )
					: _pimpl( makeImpl( opCtx, ns ) ) {}

            inline Database *db() const { return this->_impl().db(); } 

            inline Collection *getCollection() const { return this->_impl().getCollection(); }
    };
}  // namespace mongo
