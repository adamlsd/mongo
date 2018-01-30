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

#pragma once

#include "mongo/db/auth/authorization_manager.h"

#include <memory>
#include <string>

#include "mongo/base/disallow_copying.h"
#include "mongo/base/secure_allocator.h"
#include "mongo/base/status.h"
#include "mongo/bson/mutable/element.h"
#include "mongo/bson/oid.h"
#include "mongo/db/auth/action_set.h"
#include "mongo/db/auth/privilege_format.h"
#include "mongo/db/auth/resource_pattern.h"
#include "mongo/db/auth/role_graph.h"
#include "mongo/db/auth/user.h"
#include "mongo/db/auth/user_name.h"
#include "mongo/db/auth/user_name_hash.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/server_options.h"
#include "mongo/stdx/unordered_map.h"
#include "mongo/stdx/condition_variable.h"
#include "mongo/stdx/functional.h"
#include "mongo/stdx/mutex.h"

namespace mongo
{
    class AuthorizationSession;
    class AuthzManagerExternalState;
    class OperationContext;
    class ServiceContext;
    class UserDocumentParser;

    /**
     * Contains server/cluster-wide information about Authorization.
     */
    class AuthorizationManagerImpl
        : public AuthorizationManager
    {
        public:
            ~AuthorizationManagerImpl() override;

            AuthorizationManagerImpl();

			struct TestingMock { explicit TestingMock()= default; };
			AuthorizationManagerImpl( std::unique_ptr< AuthzManagerExternalState > externalState, TestingMock );

            /**
             * Returns a new AuthorizationSession for use with this AuthorizationManager.
             */
            std::unique_ptr< AuthorizationSession > makeAuthorizationSession() override;

            /**
             * Sets whether or not startup AuthSchema validation checks should be applied in this manager.
             */
            void setShouldValidateAuthSchemaOnStartup( bool validate ) override;

            /**
             * Returns true if startup AuthSchema validation checks should be applied in this manager.
             */
            bool shouldValidateAuthSchemaOnStartup() override;

            /**
             * Sets whether or not access control enforcement is enabled for this manager.
             */
            void setAuthEnabled( bool enabled ) override;

            /**
             * Returns true if access control is enabled for this manager .
             */
            bool isAuthEnabled() const override;

            /**
             * Returns via the output parameter "version" the version number of the authorization
             * system.  Returns Status::OK() if it was able to successfully fetch the current
             * authorization version.  If it has problems fetching the most up to date version it
             * returns a non-OK status.  When returning a non-OK status, *version will be set to
             * schemaVersionInvalid (0).
             */
            Status getAuthorizationVersion( OperationContext *opCtx, int *version ) override;

            /**
             * Returns the user cache generation identifier.
             */
            OID getCacheGeneration() override;

            /**
             * Returns true if there exists at least one privilege document in the system.
             * Used by the AuthorizationSession to determine whether localhost connections should be
             * granted special access to bootstrap the system.
             * NOTE: If this method ever returns true, the result is cached in _privilegeDocsExist,
             * meaning that once this method returns true it will continue to return true for the
             * lifetime of this process, even if all users are subsequently dropped from the system.
             */
            bool hasAnyPrivilegeDocuments( OperationContext *opCtx ) override;

            /**
             * Delegates method call to the underlying AuthzManagerExternalState.
             */
            Status getUserDescription( OperationContext *opCtx, const UserName &userName, BSONObj *result ) override;

            /**
             * Delegates method call to the underlying AuthzManagerExternalState.
             */
            Status getRoleDescription( OperationContext *opCtx,
                    const RoleName &roleName,
                    PrivilegeFormat privilegeFormat,
                    AuthenticationRestrictionsFormat,
                    BSONObj *result ) override;

            /**
             * Convenience wrapper for getRoleDescription() defaulting formats to kOmit.
             */
            Status
            getRoleDescription( OperationContext *ctx, const RoleName &roleName, BSONObj *result )
            {
                return getRoleDescription(
                    ctx, roleName, PrivilegeFormat::kOmit, AuthenticationRestrictionsFormat::kOmit, result );
            }

            /**
             * Delegates method call to the underlying AuthzManagerExternalState.
             */
            Status getRolesDescription( OperationContext *opCtx,
                    const std::vector< RoleName >&roleName,
                    PrivilegeFormat privilegeFormat,
                    AuthenticationRestrictionsFormat,
                    BSONObj *result ) override;

            /**
             * Delegates method call to the underlying AuthzManagerExternalState.
             */
            Status getRoleDescriptionsForDB( OperationContext *opCtx,
                    const std::string dbname,
                    PrivilegeFormat privilegeFormat,
                    AuthenticationRestrictionsFormat,
                    bool showBuiltinRoles,
                    std::vector< BSONObj > *result ) override;

            /**
             *  Returns the User object for the given userName in the out parameter "acquiredUser".
             *  If the user cache already has a user object for this user, it increments the refcount
             *  on that object and gives out a pointer to it.  If no user object for this user name
             *  exists yet in the cache, reads the user's privilege document from disk, builds up
             *  a User object, sets the refcount to 1, and gives that out.  The returned user may
             *  be invalid by the time the caller gets access to it.
             *  The AuthorizationManager retains ownership of the returned User object.
             *  On non-OK Status return values, acquiredUser will not be modified.
             */
            Status acquireUser( OperationContext *opCtx, const UserName &userName, User **acquiredUser ) override;

            /**
             * Decrements the refcount of the given User object.  If the refcount has gone to zero,
             * deletes the User.  Caller must stop using its pointer to "user" after calling this.
             */
            void releaseUser( User *user ) override;

            /**
             * Marks the given user as invalid and removes it from the user cache.
             */
            void invalidateUserByName( const UserName &user ) override;

            /**
             * Invalidates all users who's source is "dbname" and removes them from the user cache.
             */
            void invalidateUsersFromDB( const std::string &dbname ) override;

            /**
             * Initializes the authorization manager.  Depending on what version the authorization
             * system is at, this may involve building up the user cache and/or the roles graph.
             * Call this function at startup and after resynchronizing a slave/secondary.
             */
            Status initialize( OperationContext *opCtx ) override;

            /**
             * Invalidates all of the contents of the user cache.
             */
            void invalidateUserCache() override;

            /**
             * Parses privDoc and fully initializes the user object (credentials, roles, and privileges)
             * with the information extracted from the privilege document.
             * This should never be called from outside the AuthorizationManager - the only reason it's
             * public instead of private is so it can be unit tested.
             */
            Status _initializeUserFromPrivilegeDocument( User *user, const BSONObj &privDoc ) override;

            /**
             * Hook called by replication code to let the AuthorizationManager observe changes
             * to relevant collections.
             */
            void logOp( OperationContext *opCtx,
                    const char *opstr,
                    const NamespaceString &nss,
                    const BSONObj &obj,
                    const BSONObj *patt ) override;

        private:
            /**
             * Type used to guard accesses and updates to the user cache.
             */
            class CacheGuard;
            friend class AuthorizationManagerImpl::CacheGuard;

            /**
             * Invalidates all User objects in the cache and removes them from the cache.
             * Should only be called when already holding _cacheMutex.
             */
            void _invalidateUserCache_inlock();

            /**
             * Given the objects describing an oplog entry that affects authorization data, invalidates
             * the portion of the user cache that is affected by that operation.  Should only be called
             * with oplog entries that have been pre-verified to actually affect authorization data.
             */
            void _invalidateRelevantCacheData( const char *op,
                    const NamespaceString &ns,
                    const BSONObj &o,
                    const BSONObj *o2 );

            /**
             * Updates _cacheGeneration to a new OID
             */
            void _updateCacheGeneration_inlock();

            /**
             * Fetches user information from a v2-schema user document for the named user,
             * and stores a pointer to a new user object into *acquiredUser on success.
             */
            Status _fetchUserV2( OperationContext *opCtx,
                    const UserName &userName,
                    std::unique_ptr< User > *acquiredUser );

            /**
             * True if AuthSchema startup checks should be applied in this AuthorizationManager.
             *
             * Defaults to true.  Changes to its value are not synchronized, so it should only be set
             * at initalization-time.
             */
            bool _startupAuthSchemaValidation;

            /**
             * True if access control enforcement is enabled in this AuthorizationManager.
             *
             * Defaults to false.  Changes to its value are not synchronized, so it should only be set
             * at initalization-time.
             */
            bool _authEnabled;

            /**
             * A cache of whether there are any users set up for the cluster.
             */
            bool _privilegeDocsExist;

            // Protects _privilegeDocsExist
            mutable stdx::mutex _privilegeDocsExistMutex;

            std::unique_ptr< AuthzManagerExternalState > _externalState;

            /**
             * Cached value of the authorization schema version.
             *
             * May be set by acquireUser() and getAuthorizationVersion().  Invalidated by
             * invalidateUserCache().
             *
             * Reads and writes guarded by CacheGuard.
             */
            int _version;

            /**
             * Caches User objects with information about user privileges, to avoid the need to
             * go to disk to read user privilege documents whenever possible.  Every User object
             * has a reference count - the AuthorizationManager must not delete a User object in the
             * cache unless its reference count is zero.
             */
            stdx::unordered_map< UserName, User * > _userCache;

            /**
             * Current generation of cached data.  Updated every time part of the cache gets
             * invalidated.  Protected by CacheGuard.
             */
            OID _cacheGeneration;

            /**
             * True if there is an update to the _userCache in progress, and that update is currently in
             * the "fetch phase", during which it does not hold the _cacheMutex.
             *
             * Manipulated via CacheGuard.
             */
            bool _isFetchPhaseBusy;

            /**
             * Protects _userCache, _cacheGeneration, _version and _isFetchPhaseBusy.  Manipulated
             * via CacheGuard.
             */
            stdx::mutex _cacheMutex;

            /**
             * Condition used to signal that it is OK for another CacheGuard to enter a fetch phase.
             * Manipulated via CacheGuard.
             */
            stdx::condition_variable _fetchPhaseIsReady;
    };
}  // namespace mongo
