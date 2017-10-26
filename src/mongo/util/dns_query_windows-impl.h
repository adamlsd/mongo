/**
 *    Copyright (C) 2017 MongoDB Inc.
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

#ifndef MONGO_UTIL_DNS_QUERY_PLATFORM_INCLUDE_WHITELIST
#error Do not include the DNS Query platform implementation headers.  Please use "mongo/util/dns_query.h" instead.
#endif

#include <windns.h>

#include <stdio.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/noncopyable.hpp>

#include "mongo/util/errno_util.h"

using std::begin;
using std::end;
using namespace std::literals::string_literals;

namespace mongo {
namespace dns {
// The anonymous namespace is safe, in this header, as it is not really a header.  It is only used
// in the `dns_query.cpp` TU.
namespace {
enum class DNSQueryClass { kInternet };

enum class DNSQueryType { kSRV = DNS_TYPE_SRV, kTXT = DNS_TYPE_TEXT, kAddress = DNS_TYPE_A };

class ResourceRecord {
public:
    explicit ResourceRecord(std::shared_ptr<DNS_RECORDA> initialRecord)
        : _record(std::move(initialRecord)) {}
    explicit ResourceRecord() = default;

    std::vector<std::string> txtEntry() const {
        if (this->_record->wType != DNS_TYPE_TEXT) {
            std::ostringstream oss;
            oss << "Incorrect record format for \"" << this->_service
                << "\": expected TXT record, found something else";
            throw DBException(ErrorCodes::ProtocolError, oss.str());
        }

        std::vector<std::string> rv;

        const auto start = this->_record->Data.TXT.pStringArray;
        const auto count = this->_record->Data.TXT.dwStringCount;
        std::copy(start, start + count, back_inserter(rv));
        return rv;
    }

    std::string addressEntry() const {
        if (this->_record->wType != DNS_TYPE_A) {
            std::ostringstream oss;
            oss << "Incorrect record format for \"" << this->_service
                << "\": expected A record, found something else";
            throw DBException(ErrorCodes::ProtocolError, oss.str());
        }

        std::string rv;
        const auto& data = this->_record->Data.A.IpAddress;

        for (int i = 0; i < 4; ++i) {
            std::ostringstream oss;
            oss << int(data >> (i * CHAR_BIT) & 0xFF);
            rv += oss.str() + ".";
        }
        rv.pop_back();

        return rv;
    }

    SRVHostEntry srvHostEntry() const {
        if (this->_record->wType != DNS_TYPE_SRV) {
            std::ostringstream oss;
            oss << "Incorrect record format for \"" << this->_service
                << "\": expected SRV record, found something else";
            throw DBException(ErrorCodes::ProtocolError, oss.str());
        }

        const auto& data = this->_record->Data.SRV;
        return {data.pNameTarget + "."s, data.wPort};
    }

private:
    std::string _service;
    std::shared_ptr<DNS_RECORDA> _record;
};

void freeDnsRecord(PDNS_RECORDA record) {
    DnsRecordListFree(record, DnsFreeRecordList);
}

class DNSResponse {
public:
    explicit DNSResponse(PDNS_RECORDA initialResults) : _results(initialResults, freeDnsRecord) {}

    class iterator : public std::iterator<std::forward_iterator_tag, ResourceRecord> {
    public:
        explicit iterator(std::shared_ptr<DNS_RECORDA> initialRecord)
            : _record(std::move(initialRecord)) {}

        const ResourceRecord& operator*() {
            this->_populate();
            return this->_storage;
        }

        const ResourceRecord* operator->() {
            this->_populate();
            return &this->_storage;
        }

        iterator& operator++() {
            this->_advance();
            return *this;
        }

        iterator operator++(int) {
            iterator tmp = *this;
            this->_advance();
            return tmp;
        }

        auto makeRelopsLens() const {
            return this->_record.get();
        }

        inline friend bool operator==(const iterator& lhs, const iterator& rhs) {
            return lhs.makeRelopsLens() == rhs.makeRelopsLens();
        }

        inline friend bool operator<(const iterator& lhs, const iterator& rhs) {
            return lhs.makeRelopsLens() < rhs.makeRelopsLens();
        }

        inline friend bool operator!=(const iterator& lhs, const iterator& rhs) {
            return !(lhs == rhs);
        }

    private:
        void _advance() {
            this->_record = {this->_record, this->_record->pNext};
            this->_ready = false;
        }

        void _populate() {
            this->_ready = true;
            this->_storage = ResourceRecord{this->_record};
        }

        std::shared_ptr<DNS_RECORDA> _record;
        ResourceRecord _storage;
        bool _ready = false;
    };

    iterator begin() const {
        return iterator{this->_results};
    }

    iterator end() const {
        return iterator{nullptr};
    }

    std::size_t size() const {
        return std::distance(this->begin(), this->end());
    }

private:
    std::shared_ptr<std::remove_pointer<PDNS_RECORDA>::type> _results;
};

class DNSQueryState {
public:
    DNSResponse lookup(const std::string& service,
                       const DNSQueryClass class_,
                       const DNSQueryType type) {
        PDNS_RECORDA queryResults;
        auto ec = DnsQuery_UTF8(service.c_str(),
                                WORD(type),
                                DNS_QUERY_BYPASS_CACHE,
                                nullptr,
                                reinterpret_cast<PDNS_RECORD*>(&queryResults),
                                nullptr);

        if (ec) {
            throw DBException(ErrorCodes::HostNotFound,
                              "Failed to look up service \""s + errnoWithDescription(ec) + "\": "s +
                                  buffer);
        }
        return DNSResponse{queryResults};
    }
};
}  // namespace
}  // namespace dns
}  // namespace mongo
