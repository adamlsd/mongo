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
#include "mongo/util/assert_util.h"

#define UASSERT_NOT_IMPLEMENTED           \
    abort();                              \
    uasserted(ErrorCodes::NotImplemented, \
              str::stream() << "Not implemented for embedded: " << __FUNCTION__)


namespace mongo {
namespace embedded {
std::unique_ptr<mongo::AuthorizationSession> makeEmbeddedAuthorizationSession();

namespace {
class AuthorizationManager : public mongo::AuthorizationManager {
public:
    std::unique_ptr<AuthorizationSession> makeAuthorizationSession() override {
        return AuthorizationSession::create(this);
    }

    void setShouldValidateAuthSchemaOnStartup(const bool check) override {
        _shouldValidate = check;
    }
    bool shouldValidateAuthSchemaOnStartup() override {
        return _shouldValidate;
    }
    void setAuthEnabled(const bool state) override {
        invariant(!state);
    }

    bool isAuthEnabled() const override {
        return false;
    }

    Status getAuthorizationVersion(OperationContext*, int*) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    OID getCacheGeneration() override {
        UASSERT_NOT_IMPLEMENTED;
    }

    bool hasAnyPrivilegeDocuments(OperationContext*) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    Status getUserDescription(OperationContext*, const UserName&, BSONObj*) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    Status getRoleDescription(OperationContext*,
                              const RoleName&,
                              PrivilegeFormat,
                              AuthenticationRestrictionsFormat,
                              BSONObj*) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    Status getRolesDescription(OperationContext*,
                               const std::vector<RoleName>&,
                               PrivilegeFormat,
                               AuthenticationRestrictionsFormat,
                               BSONObj*) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    Status getRoleDescriptionsForDB(OperationContext*,
                                    std::string,
                                    PrivilegeFormat,
                                    AuthenticationRestrictionsFormat,
                                    bool,
                                    std::vector<BSONObj>*) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    Status acquireUser(OperationContext*, const UserName&, User**) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    void releaseUser(User* user) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    void invalidateUserByName(const UserName& user) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    void invalidateUsersFromDB(const std::string& dbname) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    Status initialize(OperationContext* opCtx) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    void invalidateUserCache() override {
        UASSERT_NOT_IMPLEMENTED;
    }

    Status _initializeUserFromPrivilegeDocument(User*, const BSONObj&) override {
        UASSERT_NOT_IMPLEMENTED;
    }

    void logOp(OperationContext*,
               const char*,
               const NamespaceString&,
               const BSONObj&,
               const BSONObj*) override { /* do nothing*/
    }

private:
    bool _shouldValidate = false;
};
}  // namespace
}  // namespace embedded

MONGO_REGISTER_SHIM(AuthorizationManager::create)()->std::unique_ptr<AuthorizationManager> {
    return std::make_unique<embedded::AuthorizationManager>();
}
}  // namespace mongo
