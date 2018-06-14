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
    /**
     * Constructs a parsed DNS Hostname representation from the specified string.
     * A DNS name can be fully qualified (ending in a '.') or unqualified (not ending in a '.').
     *
     * THROWS: `DBException` with `ErrorCodes::DNSRecordTypeMismatch` as the status value if the
     * name is ill formatted.
     */
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

    /**
     * Returns `true` if this DNS Hostname has been fully qualified, and false otherwise.
     *
     * A DNS Hostname is considered fully qualified, if the canonical specification of its name
     * includes a trailing `'.'`.  Fully Qualified Domain Names (FQDNs) are always resolved against
     * the root name servers and indicate absolute names.  Unqualified names are looked up against
     * DNS configuration specific prefixes, recursively, until a match is found, which may not be
     * the corresponding FQDN.
     *
     * RETURNS: True if this hostname is an FQDN and false otherwise.
     */
    bool isFQDN() const {
        return fullyQualified;
    }

    /**
     * Changes the qualification of this `dns::HostName` to the specified `qualification`.
     *
     * An unqualified domain hostname may exist as an artifact of other protocols wherein the actual
     * qualification of that name is implied to be complete.  When operating on such names in
     * `dns::HostName` form, it may be necessary to alter the qualification after the fact.
     *
     * POST: The qualification of `*this` will be changed to the qualification specified by
     * `qualification`.
     */
    void forceQualification(const Qualification qualification = kFullyQualified) {
        fullyQualified = qualification;
    }

    /**
     * Returns the complete canonical name for this `dns::HostName` as a `std::string` object.
     *
     * The canonical form for a DNS Hostname is the complete dotted DNS path, including a trailing
     * dot (if the domain in question is fully qualified).  A DNS Hostname which is fully qualified
     * (ending in a trailing dot) will not compare equal (in string form) to a DNS Hostname which
     * has not been fully qualified.  This representation may be unsuitable for some use cases which
     * involve relaxed qualification indications.
     *
     * RETURNS: A `std::string` which represents this DNS Hostname in complete canonical form.
     */
    std::string canonicalName() const {
        return str::stream() << *this;
    }

    /**
     * Returns the complete name for this `dns::HostNmae` as a `std::string` object, in a form
     * suitable
     * for use with SSL certificate names.
     *
     * For myriad reasons, SSL certificates do not specify the fully qualified name of any host.
     * When using `dns::HostName` objects in SSL aware code, it may be necessary to get an
     * unqualified string form for use in certificate name comparisons.
     *
     * RETURNS: A `std::string` which represents this DNS Hostname without qualification indication.
     */
    std::string sslName() const {
        StringBuilder sb;
        streamUnqualified(sb);
        return sb.str();
    }

    /**
     * Returns the number of subdomain components in this `dns::HostName`.
     *
     * A DNS Hostname is composed of at least one, and sometimes more, subdomains.  This function
     * indicates how many subdomains this `dns::HostName` specifier has.  Each subdomain is
     * separated by a single `'.'` character.
     *
     * RETURNS: The number of components in `this->nameComponents()`
     */
    std::size_t depth() const {
        return this->_nameComponents.size();
    }

    /**
     * Returns a new `dns::HostName` object which represents the name of the DNS domain in which
     * this object resides.
     *
     * All domains of depth greater than 1 are composed of multiple sub-domains.  This function
     * provides the next-level parent of the domain represented by `*this`.
     *
     * PRE: This `dns::HostName` must have at least two subdomains (`this->depth() > 1`).
     *
     * NOTE: The behavior of this function is undefined unless its preconditions are met.
     *
     * RETURNS: A `dns::HostName` which has one fewer domain specified.
     */
    HostName parentDomain() const {
        if (this->_nameComponents.size() == 1) {
            uasserted(ErrorCodes::DNSRecordTypeMismatch,
                      "A top level domain has no subdomains in its name");
        }
        HostName result = *this;
        result._nameComponents.pop_back();
        return result;
    }

    /**
     * Returns true if the specified `candidate` Hostname would be resolved within `*this` as a
     * hostname, and false otherwise.
     *
     * Two domains can be said to have a "contains" relationship only when when both are Fully
     * Qualified Domain Names (FQDNs).  When either domain or both domains are unqualified, then it
     * is impossible to know whether one could be resolved within the other correctly.
     *
     * RETURNS: False when `!candidate.isFQDN() || this->isFQDN()`.  False when `this->depth() >=
     * candidate.depth()`.  Otherwise a value equivalent to `[temp = candidate]{ while (temp.depth()
     * > this->depth()) temp= temp.parentDomain(); return temp; }() == *this;`
     */
    bool contains(const HostName& candidate) const {
        return (fullyQualified == candidate.fullyQualified) &&
            (_nameComponents.size() < candidate._nameComponents.size()) &&
            std::equal(
                   begin(_nameComponents), end(_nameComponents), begin(candidate._nameComponents));
    }

    /**
     * Returns a new `dns::HostName` which represents the larger (possibly canonical) name that
     * would be used to lookup `*this` within the domain of the specified `rhs`.
     *
     * Unqualified DNS Hostnames can be prepended to other DNS Hostnames to provide a DNS string
     * which is equivalent to what a resolution of the unqualified name would be in the domain of
     * the second (possibly qualified) name.
     *
     * PRE: `this->isFQDN() == false`.
     *
     * RETURNS: A `dns::HostName` which has a `canonicalName()` equivalent to `*this.canonicalName()
     * +
     * rhs.canonicalName()`.
     *
     * THROWS: `DBException` with `ErrorCodes::DNSRecordTypeMismatch` as the status value if
     * `this->isFQDN() == false`.
     */
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

    /**
     * Returns an immutable reference to a `std::vector` of `std::string` which indicates the
     * canonical path of this `dns::HostName`.
     *
     * Sometimes it is necessary to iterate over all of the elements of a domain name string.  This
     * function facilitates such iteration.
     *
     * RETURNS: A `const std::vector<std::string>&` which refers to all of the domain name
     * components of `*this`.
     */

    const std::vector<std::string>& nameComponents() const& {
        return this->_nameComponents;
    }

    void nameComponents() && = delete;

    const std::vector<std::string>& altRvalueNameComponents(
        ArgGuard = {}, std::vector<std::string>&& preserve = {})&& {
        preserve = std::move(this->_nameComponents);
        return preserve;
    }

    /**
     * Returns true if the specified `dns::HostName`s, `lhs` and `rhs` represent the same DNS path,
     * and fase otherwise.
     * RETURNS: True if `lhs` and `rhs` represent the same DNS path, and false otherwise.
     */
    friend bool operator==(const HostName& lhs, const HostName& rhs) {
        return lhs.make_equality_lens() == rhs.make_equality_lens();
    }

    /**
     * Returns true if the specified `dns::HostName`s, `lhs` and `rhs` do not represent the same DNS
     * path, and fase otherwise.
     * RETURNS: True if `lhs` and `rhs` do not represent the same DNS path, and false otherwise.
     */
    friend bool operator!=(const HostName& lhs, const HostName& rhs) {
        return !(lhs == rhs);
    }

    /**
     * Streams a representation of the specified `hostName` to the specified `os` formatting stream.
     *
     * A canonical representation of `hostName` (with a trailing dot, `'.'`, when `hostName.isFQDN()
     * == true`) will be placed into the formatting stream handled by `os`.
     *
     * RETURNS: A reference to the specified output stream `os`.
     */
    friend std::ostream& operator<<(std::ostream& os, const HostName& hostName) {
        if (hostName.fullyQualified) {
            hostName.streamQualified(os);
        } else {
            hostName.streamUnqualified(os);
        }

        return os;
    }

    /**
     * Streams a representation of the specified `hostName` to the specified `os` formatting stream.
     *
     * A canonical representation of `hostName` (with a trailing dot, `'.'`, when `hostName.isFQDN()
     * == true`) will be placed into the formatting stream handled by `os`.
     *
     * RETURNS: A reference to the specified output stream `os`.
     */
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
