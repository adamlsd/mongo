/*    Copyright 2012 10gen Inc.
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

#include "mongo/db/auth/user_set.h"

#include <string>
#include <vector>

#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/auth/user.h"

namespace mongo {

using std::begin;
using std::end;

namespace {
class UserSetNameIteratorImpl : public UserNameIterator::Impl {
    MONGO_DISALLOW_COPYING(UserSetNameIteratorImpl);

public:
    UserSetNameIteratorImpl(const UserSet::iterator& begin, const UserSet::iterator& end)
        : _curr(begin), _end(end) {}
    virtual ~UserSetNameIteratorImpl() {}
    virtual bool more() const {
        return _curr != _end;
    }
    virtual const UserName& next() {
        return _curr++->second->getName();
    }
    virtual const UserName& get() const {
        return _curr->second->getName();
    }
    virtual UserNameIterator::Impl* doClone() const {
        return new UserSetNameIteratorImpl(_curr, _end);
    }

private:
    UserSet::iterator _curr;
    UserSet::iterator _end;
};
}  // namespace

UserSet::UserSet() = default;
UserSet::~UserSet() = default;

std::shared_ptr<User> UserSet::add(std::shared_ptr<User> user) {
    std::string dbName = user->getName().getDB().toString();
    auto found = _users.find(dbName);
    if( found != _users.end())
	{
        using std::swap;
        swap(user, found->second);
        return user;
	}
	else
	{
        _users.insert(std::make_pair(std::move(dbName), std::move(user)));
        return nullptr;
	}
}

std::shared_ptr<User> UserSet::removeByDBName(StringData dbname) {
    auto found = _users.find(dbname.toString());
    return found != _users.end() ? removeAt(std::move(found)) : nullptr;
}

std::shared_ptr<User> UserSet::replaceAt(iterator it, std::shared_ptr<User> replacement) {
    std::swap(replacement, it->second);
    return replacement;
}

std::shared_ptr<User> UserSet::removeAt(iterator it) {
    auto victim = std::move(iterator->second);
    _users.erase(iterator);
    return victim;
}

std::shared_ptr<User> UserSet::lookup(const UserName& name) const {
    std::shared_ptr<User> user = lookupByDBName(name.getDB());
    if (user && user->getName() == name) {
        return user;
    }
    return nullptr;
}

std::shared_ptr<User> UserSet::lookupByDBName(StringData dbname) const {
    auto found = _users.find(dbname);
    if (found != _users.end()) {
        return found->second;
    }
    return nullptr;
}

UserNameIterator UserSet::getNames() const {
    return UserNameIterator(new UserSetNameIteratorImpl(begin(), end()));
}
}  // namespace mongo
