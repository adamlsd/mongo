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
#include "mongo/platform/basic.h"
#include "mongo/util/dns_query.h"

#ifndef _WIN32
// DNS Headers for POSIX/libresolv have to be included in a specific order
// clang-format off
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
// clang-format on
#else
#include <windns.h>
#endif

#include <stdio.h>

#include <iostream>
#include <cassert>
#include <sstream>
#include <string>
#include <cstdint>
#include <vector>
#include <array>
#include <stdexcept>
#include <memory>
#include <exception>

#include <boost/noncopyable.hpp>

using std::begin;
using std::end;
using namespace std::literals::string_literals;

namespace mongo {
namespace dns {
namespace {
#ifndef _WIN32
enum class DNSQueryClass {
    kInternet = ns_c_in,
};

enum class DNSQueryType {
    kSRV = ns_t_srv,
    kTXT = ns_t_txt,
    kAddress = ns_t_a,
};

class ResourceRecord {
public:
    explicit ResourceRecord() = default;

    explicit ResourceRecord(std::string initialService, ns_msg& ns_answer, const int initialPos)
        : service(std::move(initialService)),
          answerStart(ns_msg_base(ns_answer)),
          answerEnd(ns_msg_end(ns_answer)),
          pos(initialPos) {
        if (ns_parserr(&ns_answer, ns_s_an, initialPos, &resource_record))
            badRecord();
    }

    std::vector<std::string> txtEntry() const {
        const auto data = rawData();
        const std::size_t amt = data.front();
        const auto first = begin(data) + 1;
        std::vector<std::string> rv;
        rv.emplace_back(first, first + amt);
        return rv;
    }

    std::string addressEntry() const {
        std::string rv;

        auto data = rawData();
        if (data.size() != 4) {
            throw DBException(ErrorCodes::ProtocolError, "DNS A Record is not correctly sized");
        }
        for (const std::uint8_t& ch : data) {
            std::ostringstream oss;
            oss << int(ch);
            rv += oss.str() + ".";
        }
        rv.pop_back();
        return rv;
    }

    SRVHostEntry srvHostEntry() const {
        const std::uint8_t* const data = ns_rr_rdata(resource_record);
        const uint16_t port = ntohs(*reinterpret_cast<const short*>(data + 4));

        std::string name;
        name.resize(8192, '@');

        const auto size = dn_expand(answerStart, answerEnd, data + 6, &name[0], name.size());

        if (size < 1)
            badRecord();

        // Trim the expanded name
        name.resize(name.find('\0'));
        name += '.';

        // return by copy is equivalent to a `shrink_to_fit` and `move`.
        return {name, port};
    }

private:
    void badRecord() const {
        std::ostringstream oss;
        oss << "Invalid record " << pos << " of SRV answer for \"" << service << "\": \""
            << strerror(errno) << "\"";
        throw DBException(ErrorCodes::ProtocolError, oss.str());
    };

    std::vector<std::uint8_t> rawData() const {
        const std::uint8_t* const data = ns_rr_rdata(resource_record);
        const std::size_t length = ns_rr_rdlen(resource_record);

        return {data, data + length};
    }

    std::string service;
    ns_rr resource_record;
    const std::uint8_t* answerStart;
    const std::uint8_t* answerEnd;
    int pos;
};

class DNSResponse {
public:
    explicit DNSResponse(std::string initialService, std::vector<std::uint8_t> initialData)
        : service(std::move(initialService)), data(std::move(initialData)) {
        if (ns_initparse(data.data(), data.size(), &ns_answer)) {
            std::ostringstream oss;
            oss << "Invalid SRV answer for \"" << service << "\"";
            throw DBException(ErrorCodes::ProtocolError, oss.str());
        }

        nRecords = ns_msg_count(ns_answer, ns_s_an);

        if (!nRecords) {
            std::ostringstream oss;
            oss << "No SRV records for \"" << service << "\"";
            throw DBException(ErrorCodes::ProtocolError, oss.str());
        }
    }

    class iterator {
    private:
        DNSResponse* response;
        int pos = 0;
        ResourceRecord record;
        bool ready = false;

        friend DNSResponse;

        explicit iterator(DNSResponse* const r)
            : response(r), record(this->response->service, this->response->ns_answer, 0) {}

        explicit iterator(DNSResponse* const initialResponse, int initialPos)
            : response(initialResponse), pos(initialPos) {}

        void hydrate() {
            if (ready)
                return;
            record = ResourceRecord(this->response->service, this->response->ns_answer, this->pos);
        }

        void advance() {
            ++this->pos;
            ready = false;
        }

        auto make_relops_lens() const {
            return std::tie(this->response, this->pos);
        }

    public:
        inline friend bool operator==(const iterator& lhs, const iterator& rhs) {
            return lhs.make_relops_lens() == rhs.make_relops_lens();
        }

        inline friend bool operator<(const iterator& lhs, const iterator& rhs) {
            return lhs.make_relops_lens() < rhs.make_relops_lens();
        }

        inline friend bool operator!=(const iterator& lhs, const iterator& rhs) {
            return !(lhs == rhs);
        }

        const ResourceRecord& operator*() {
            this->hydrate();
            return this->record;
        }

        const ResourceRecord* operator->() {
            this->hydrate();
            return &this->record;
        }

        iterator& operator++() {
            this->advance();
            return *this;
        }

        iterator operator++(int) {
            iterator tmp = *this;
            this->advance();
            return tmp;
        }
    };

    auto begin() {
        return iterator(this);
    }

    auto end() {
        return iterator(this, this->nRecords);
    }

    std::size_t size() const {
        return this->nRecords;
    }

private:
    std::string service;
    std::vector<std::uint8_t> data;
    ns_msg ns_answer;
    std::size_t nRecords;
};

/**
 * The `DNSQueryState` object represents the state of a DNS query interface, on Unix-like systems.
 */
class DNSQueryState : boost::noncopyable {
public:
    std::vector<std::uint8_t> raw_lookup(const std::string& service,
                                         const DNSQueryClass class_,
                                         const DNSQueryType type) {
        std::vector<std::uint8_t> result(65536);
#ifdef MONGO_HAVE_RES_NQUERY
        const int size =
            res_nsearch(&state, service.c_str(), int(class_), int(type), &result[0], result.size());
#else
        const int size =
            res_search(service.c_str(), int(class_), int(type), &result[0], result.size());
#endif

        if (size < 0) {
            std::ostringstream oss;
            oss << "Failed to look up service \"" << service << "\": " << strerror(errno);
            throw DBException(ErrorCodes::HostNotFound, oss.str());
        }
        result.resize(size);

        return result;
    }

    DNSResponse lookup(const std::string& service,
                       const DNSQueryClass class_,
                       const DNSQueryType type) {
        return DNSResponse(service, raw_lookup(service, class_, type));
    }

#ifdef MONGO_HAVE_RES_NQUERY
public:
    ~DNSQueryState() {
#ifdef MONGO_HAVE_RES_NDESTROY
        res_ndestroy(&state);
#elif defined(MONGO_HAVE_RES_NCLOSE)
        res_nclose(&state);
#endif
    }

    DNSQueryState() : state() {
        res_ninit(&state);
    }

private:
    struct __res_state state;
#endif
};

#else

enum class DNSQueryClass { kInternet };

enum class DNSQueryType { kSRV = DNS_TYPE_SRV, kTXT = DNS_TYPE_TEXT, kAddress = DNS_TYPE_A };

class ResourceRecord {
public:
    explicit ResourceRecord(std::shared_ptr<DNS_RECORDA> initialRecord)
        : record(std::move(initialRecord)) {}
    explicit ResourceRecord() = default;

    std::vector<std::string> txtEntry() const {
        if (record->wType != DNS_TYPE_TEXT) {
            std::ostringstream oss;
            oss << "Incorrect record format for \"" << service
                << "\": expected TXT record, found something else";
            throw DNSLookupException(oss.str());
        }

        std::vector<std::string> rv;

        const auto start = record->Data.TXT.pStringArray;
        const auto count = record->Data.TXT.dwStringCount;
        std::copy(start, start + count, back_inserter(rv));
        return rv;
    }

    std::string addressEntry() const {
        if (record->wType != DNS_TYPE_A) {
            std::ostringstream oss;
            oss << "Incorrect record format for \"" << service
                << "\": expected A record, found something else";
            throw DNSLookupException(oss.str());
        }

        std::string rv;
        auto data = record->Data.A.IpAddress;

        for (int i = 0; i < 4; ++i) {
            std::ostringstream oss;
            oss << int(data >> (i * CHAR_BIT) & 0xFF);
            rv += oss.str() + ".";
        }
        rv.pop_back();

        return rv;
    }

    SRVHostEntry srvHostEntry() const {
        if (record->wType != DNS_TYPE_SRV) {
            std::ostringstream oss;
            oss << "Incorrect record format for \"" << service
                << "\": expected SRV record, found something else";
            throw DNSLookupException(oss.str());
        }

        const auto& data = record->Data.SRV;
        return {data.pNameTarget + "."s, data.wPort};
    }

private:
    std::string service;
    std::shared_ptr<DNS_RECORDA> record;
};

void freeDnsRecord(PDNS_RECORDA record) {
    DnsRecordListFree(record, DnsFreeRecordList);
}

class DNSResponse {
public:
    explicit DNSResponse(PDNS_RECORDA initialResults) : results(initialResults, freeDnsRecord) {}

    class iterator : public std::iterator<std::forward_iterator_tag, ResourceRecord> {
    public:
        explicit iterator(std::shared_ptr<DNS_RECORDA> initialRecord)
            : record(std::move(initialRecord)) {}

        const ResourceRecord& operator*() {
            this->hydrate();
            return this->storage;
        }

        const ResourceRecord* operator->() {
            this->hydrate();
            return &this->storage;
        }

        iterator& operator++() {
            this->advance();
            return *this;
        }

        iterator operator++(int) {
            iterator tmp = *this;
            this->advance();
            return tmp;
        }

        auto make_relops_lens() const {
            return this->record.get();
        }

        inline friend bool operator==(const iterator& lhs, const iterator& rhs) {
            return lhs.make_relops_lens() == rhs.make_relops_lens();
        }

        inline friend bool operator<(const iterator& lhs, const iterator& rhs) {
            return lhs.make_relops_lens() < rhs.make_relops_lens();
        }

        inline friend bool operator!=(const iterator& lhs, const iterator& rhs) {
            return !(lhs == rhs);
        }

    private:
        std::shared_ptr<DNS_RECORDA> record;
        ResourceRecord storage;
        bool ready = false;

        void advance() {
            record = {record, record->pNext};
            ready = false;
        }

        void hydrate() {
            ready = true;
            storage = ResourceRecord{record};
        }
    };

    iterator begin() const {
        return iterator(results);
    }

    iterator end() const {
        return iterator{nullptr};
    }

    std::size_t size() const {
        return std::distance(this->begin(), this->end());
    }

private:
    std::shared_ptr<std::remove_pointer<PDNS_RECORDA>::type> results;
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
            std::string buffer;
            buffer.resize(64 * 1024);
            LPVOID msgBuf = &buffer[0];
            auto count = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM || FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr,
                                       ec,
                                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                       reinterpret_cast<LPTSTR>(&msgBuf),
                                       buffer.size(),
                                       nullptr);

            if (count)
                buffer.resize(count);
            else
                buffer = "Unknown error";
            throw DNSLookupNotFoundException("Failed to look up service \""s + service + "\": "s +
                                             buffer);
        }
        return DNSResponse{queryResults};
    }
};
#endif
}  // namespace
}  // namespace dns

/**
 * Returns a string with the IP address or domain name listed...
 */
std::vector<std::string> dns::lookupARecords(const std::string& service) {
    DNSQueryState dnsQuery;
    auto response = dnsQuery.lookup(service, DNSQueryClass::kInternet, DNSQueryType::kAddress);

    if (response.size() == 0) {
        throw DBException(ErrorCodes::ProtocolError,
                          "Looking up " + service + " A record no results.");
    }

    std::vector<std::string> rv;
    std::transform(begin(response), end(response), back_inserter(rv), [](const auto& entry) {
        return entry.addressEntry();
    });

    return rv;
}

std::vector<dns::SRVHostEntry> dns::lookupSRVRecords(const std::string& service) {
    DNSQueryState dnsQuery;

    auto response = dnsQuery.lookup(service, DNSQueryClass::kInternet, DNSQueryType::kSRV);

    std::vector<SRVHostEntry> rv;

    std::transform(begin(response), end(response), back_inserter(rv), [](const auto& entry) {
        return entry.srvHostEntry();
    });
    return rv;
}

std::vector<std::string> dns::lookupTXTRecords(const std::string& service) {
    DNSQueryState dnsQuery;

    auto response = dnsQuery.lookup(service, DNSQueryClass::kInternet, DNSQueryType::kTXT);

    std::vector<std::string> rv;

    for (auto& entry : response) {
        auto txtEntry = entry.txtEntry();
        rv.insert(end(rv),
                  std::make_move_iterator(begin(txtEntry)),
                  std::make_move_iterator(end(txtEntry)));
    }
    return rv;
}

std::vector<std::string> dns::getTXTRecords(const std::string& service) try {
    return lookupTXTRecords(service);
} catch (const DBException& ex) {
    if (ex.code() == ErrorCodes::HostNotFound) {
        return {};
    }
    throw;
}
}  // namespace mongo
