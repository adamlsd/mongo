/**
 *    Copyright (C) 2018 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
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

#include <algorithm>
#include <iostream>
#include <iterator>
#include <sstream>
#include <tuple>
#include <vector>

#include "mongo/base/error_codes.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/util/builder.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {
namespace dns {
namespace detail_dns_host_name {
using std::begin;
using std::end;
class HostName {
public:
    enum Qualification : bool { kRelativeName = false, kFullyQualified = true };

private:
    class ArgGuard {
        friend HostName;
        ArgGuard() = default;
    };

    // Hostname components are stored in hierarchy order (reverse from how read by humans in text)
    std::vector<std::string> _nameComponents;
    Qualification fullyQualified;

    auto make_equality_lens() const {
        return std::tie(fullyQualified, _nameComponents);
    }

    template <typename StreamLike>
    void streamCore(StreamLike& os) const {
        std::for_each(rbegin(_nameComponents),
                      rend(_nameComponents),
                      [ first = true, &os ](const auto& component) mutable {
                          if (!first)
                              os << '.';
                          first = false;
                          os << component;
                      });
    }

    template <typename StreamLike>
    void streamQualified(StreamLike& os) const {
        invariant(fullyQualified);
        streamCore(os);
        os << '.';
    }

    template <typename StreamLike>
    void streamUnqualified(StreamLike& os) const {
        streamCore(os);
    }

    // If there are exactly 4 name components, and they are not fully qualified, then they cannot be
    // all numbers.
    void checkForValidForm() const {
        if (this->_nameComponents.size() != 4)
            return;
        if (this->fullyQualified)
            return;

        for (const auto& name : this->_nameComponents) {
            // Any letters are good.
            if (end(name) != std::find_if(begin(name), end(name), isalpha))
                return;
            // A hyphen is okay too.
            if (end(name) != std::find(begin(name), end(name), '-'))
                return;
        }

        // If we couldn't find any letters or hyphens
        uasserted(ErrorCodes::DNSRecordTypeMismatch,
                  "A Domain Name cannot be equivalent in form to an IPv4 address");
    }

public:
    template <typename StringIter>
    HostName(const StringIter first,
             const StringIter second,
             const Qualification qualification = kRelativeName)
        : _nameComponents(first, second), fullyQualified(qualification) {
        if (_nameComponents.empty())
            uasserted(ErrorCodes::DNSRecordTypeMismatch,
                      "A Domain Name cannot have zero name elements");
        checkForValidForm();
    }

    explicit HostName(StringData dnsName) {
        if (dnsName.empty())
            uasserted(ErrorCodes::DNSRecordTypeMismatch,
                      "A Domain Name cannot have zero characters");

        if (dnsName[0] == '.')
            uasserted(ErrorCodes::DNSRecordTypeMismatch,
                      "A Domain Name cannot start with a '.' character.");

        enum ParserState { kNonPeriod = 1, kPeriod = 2 };
        ParserState parserState = kNonPeriod;

        std::string name;
        for (const char ch : dnsName) {
            if (ch == '.') {
                if (parserState == kPeriod)
                    uasserted(ErrorCodes::DNSRecordTypeMismatch,
                              "A Domain Name cannot have two adjacent '.' characters");
                parserState = kPeriod;
                _nameComponents.push_back(std::move(name));
                name.clear();
                continue;
            }
            parserState = kNonPeriod;

            name.push_back(ch);
        }

        if (parserState == kPeriod)
            fullyQualified = kFullyQualified;
        else {
            fullyQualified = kRelativeName;
            _nameComponents.push_back(std::move(name));
        }

        if (_nameComponents.empty())
            uasserted(ErrorCodes::DNSRecordTypeMismatch,
                      "A Domain Name cannot have zero name elements");

        checkForValidForm();

        // Reverse all the names, once we've parsed them all in.
        std::reverse(begin(_nameComponents), end(_nameComponents));
    }

    bool isFQDN() const {
        return fullyQualified;
    }

    void forceQualification(const Qualification qualification = kFullyQualified) {
        fullyQualified = qualification;
    }

    std::string canonicalName() const {
        return str::stream() << *this;
    }

    std::string sslName() const {
        StringBuilder sb;
        streamUnqualified(sb);
        return sb.str();
    }

    HostName parentDomain() const {
        if (this->_nameComponents.size() == 1) {
            uasserted(ErrorCodes::DNSRecordTypeMismatch,
                      "A top level domain has no subdomains in its name");
        }
        HostName result = *this;
        result._nameComponents.pop_back();
        return result;
    }

    bool contains(const HostName& candidate) const {
        return (fullyQualified == candidate.fullyQualified) &&
            (_nameComponents.size() < candidate._nameComponents.size()) &&
            std::equal(
                   begin(_nameComponents), end(_nameComponents), begin(candidate._nameComponents));
    }

    HostName resolvedIn(const HostName& rhs) const {
        if (this->fullyQualified)
            uasserted(
                ErrorCodes::DNSRecordTypeMismatch,
                "A fully qualified Domain Name cannot be resolved within another domain name.");
        HostName result = rhs;
        result._nameComponents.insert(
            end(result._nameComponents), begin(this->_nameComponents), end(this->_nameComponents));

        return result;
    }

    const std::vector<std::string>& altRvalueNameComponents(
        ArgGuard = {}, std::vector<std::string>&& preserve = {})&& {
        preserve = std::move(this->_nameComponents);
        return preserve;
    }
    void nameComponents() && = delete;

    const std::vector<std::string>& nameComponents() const& {
        return this->_nameComponents;
    }

    friend bool operator==(const HostName& lhs, const HostName& rhs) {
        return lhs.make_equality_lens() == rhs.make_equality_lens();
    }

    friend bool operator!=(const HostName& lhs, const HostName& rhs) {
        return !(lhs == rhs);
    }

    friend std::ostream& operator<<(std::ostream& os, const HostName& hostName) {
        if (hostName.fullyQualified) {
            hostName.streamQualified(os);
        } else {
            hostName.streamUnqualified(os);
        }

        return os;
    }

    friend StringBuilder& operator<<(StringBuilder& os, const HostName& hostName) {
        if (hostName.fullyQualified) {
            hostName.streamQualified(os);
        } else {
            hostName.streamUnqualified(os);
        }

        return os;
    }
};
}  // detail_dns_host_name
using detail_dns_host_name::HostName;
}  // namespace dns
}  // namespace mongo
