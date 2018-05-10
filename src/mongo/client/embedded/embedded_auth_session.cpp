/**
 *    Copyright (C) 2018 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/auth/authorization_session.h"

namespace mongo
{
    
	namespace embedded
	{
        namespace
        {
            class AuthorizationSession : public mongo::AuthorizationSession
            {
                public:
                    explicit AuthorizationSession( AuthorizationManager *const authzManager )
                            : _authzManager( authzManager ) {}

                    AuthorizationManager& getAuthorizationManager() override { abort(); }

                    void startRequest(OperationContext* ) override { abort(); }

                    Status addAndAuthorizeUser(OperationContext* , const UserName& ) override { abort(); }

                    User* lookupUser(const UserName& ) override { abort(); }

                    User* getSingleUser() override { abort(); }

                    bool isAuthenticated() override { abort(); }

                    UserNameIterator getAuthenticatedUserNames() override { abort(); }

                    RoleNameIterator getAuthenticatedRoleNames() override { abort(); }

                    std::string getAuthenticatedUserNamesToken() override { abort(); }

                    void grantInternalAuthorization() override { abort(); }

                    void logoutDatabase(const std::string& ) override { abort(); }

                    PrivilegeVector getDefaultPrivileges() override { abort(); }

                    Status checkAuthForFind(const NamespaceString& , bool ) override { abort(); }

                    Status checkAuthForGetMore(const NamespaceString& ,
                                                       long long ,
                                                       bool ) override { abort(); }

                    Status checkAuthForUpdate(OperationContext* ,
                                                      const NamespaceString& ,
                                                      const BSONObj& ,
                                                      const BSONObj& ,
                                                      bool ) override { abort(); }

                    Status checkAuthForInsert(OperationContext* ,
                                                      const NamespaceString& ,
                                                      const BSONObj& ) override { abort(); }

                    Status checkAuthForDelete(OperationContext* ,
                                                      const NamespaceString& ,
                                                      const BSONObj& ) override { abort(); }

                    Status checkAuthForKillCursors(const NamespaceString& ,
                                                           UserNameIterator ) override {
abort(); }

                    Status checkAuthForAggregate(const NamespaceString& ,
                                                         const BSONObj& ,
                                                         bool ) override { abort(); }

                    Status checkAuthForCreate(const NamespaceString& ,
                                                      const BSONObj& ,
                                                      bool ) override { abort(); }

                    Status checkAuthForCollMod(const NamespaceString& ,
                                                       const BSONObj& ,
                                                       bool ) override { abort(); }

                    Status checkAuthorizedToGrantPrivilege(const Privilege& )
override { abort(); }

                    Status checkAuthorizedToRevokePrivilege(const Privilege& )
override { abort(); }

                    bool isUsingLocalhostBypass() override { abort(); }

                    bool isAuthorizedToParseNamespaceElement(const BSONElement& )
override { abort(); }

                    bool isAuthorizedToCreateRole(const auth::CreateOrUpdateRoleArgs& )
override { abort(); }

                    bool isAuthorizedToGrantRole(const RoleName& ) override { abort(); }

                    bool isAuthorizedToRevokeRole(const RoleName& ) override { abort(); }

                    bool isAuthorizedToChangeAsUser(const UserName& , ActionType )
override { abort(); }

                    bool isAuthorizedToChangeOwnPasswordAsUser(const UserName& ) override {
abort(); }

                    bool isAuthorizedToListCollections(StringData ) override { abort(); }

                    bool isAuthorizedToChangeOwnCustomDataAsUser(const UserName& ) override
{ abort(); }

                    bool isAuthenticatedAsUserWithRole(const RoleName& ) override { abort();
}

                    bool isAuthorizedForPrivilege(const Privilege& ) override { abort(); }

                    bool isAuthorizedForPrivileges(const std::vector<Privilege>& )
override { abort(); }

                    bool isAuthorizedForActionsOnResource(const ResourcePattern& ,
                                                                  ActionType ) override {
abort(); }

                    bool isAuthorizedForActionsOnResource(const ResourcePattern& ,
                                                                  const ActionSet& ) override
{ abort(); }

                    bool isAuthorizedForActionsOnNamespace(const NamespaceString& ,
                                                                   ActionType ) override {
abort(); }

                    bool isAuthorizedForActionsOnNamespace(const NamespaceString& ,
                                                                   const ActionSet& )
override { abort(); }

                    void setImpersonatedUserData(std::vector<UserName> ,
                                                         std::vector<RoleName> ) override {
abort(); }

                    UserNameIterator getImpersonatedUserNames() override { abort(); }

                    RoleNameIterator getImpersonatedRoleNames() override { abort(); }

                    void clearImpersonatedUserData() override { abort(); }

                    bool isCoauthorizedWithClient(Client* ) override { abort(); }

                    bool isCoauthorizedWith(UserNameIterator ) override { abort(); }

                    bool isImpersonating() const override { abort(); }

                    Status checkCursorSessionPrivilege( OperationContext* , boost::optional<LogicalSessionId> ) override { abort(); }

                protected:
                    std::tuple<std::vector<UserName>*, std::vector<RoleName>*> _getImpersonations() override { abort(); }

                private:
                    AuthorizationManager *const _authzManager;
            };
        }//namespace
    }//namespace embedded

    MONGO_REGISTER_SHIM( AuthorizationSession::create )(AuthorizationManager *const authzManager)
            -> std::unique_ptr< AuthorizationSession >
    {
        return std::make_unique< embedded::AuthorizationSession >(authzManager);
    }
}//namespace mongo
