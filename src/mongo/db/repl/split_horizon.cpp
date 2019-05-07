/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kReplication

#include "mongo/platform/basic.h"

#include "mongo/db/repl/split_horizon.h"

#include <utility>

#include "mongo/bson/util/bson_extract.h"
#include "mongo/db/client.h"
#include "mongo/util/log.h"

namespace mongo {
namespace repl {
namespace {
const auto getSplitHorizonParameters = Client::declareDecoration<SplitHorizon::Parameters>();
}  // namespace

void SplitHorizon::setParameters(Client* const client,
                                 boost::optional<std::string> connectionTarget) {
    stdx::lock_guard<Client> lk(*client);
    getSplitHorizonParameters(*client) = {std::move(connectionTarget)};
}

auto SplitHorizon::getParameters(const Client* const client) -> Parameters {
    return getSplitHorizonParameters(*client);
}

StringData SplitHorizon::determineHorizon(const int incomingPort,
                                          const SplitHorizon::Parameters& horizonParameters) const {
    if (horizonParameters.connectionTarget) {
        const HostAndPort connectionTarget(*horizonParameters.connectionTarget);
        auto found = reverseMapping.find(connectionTarget);
        if (found != end(reverseMapping))
            return found->second;
    }
    return kDefaultHorizon;
}

void SplitHorizon::toBSON(const ReplSetTagConfig& tagConfig, BSONObjBuilder& configBuilder) const {
    // `forwardMapping` should always contain the "__default" horizon, so we need to emit the
    // horizon repl specification when there are OTHER horizons.
    if (this->forwardMapping.size() > 1) {
        StringMap<std::tuple<HostAndPort, int>> horizons;
        std::transform(begin(this->forwardMapping),
                       end(this->forwardMapping),
                       inserter(horizons, end(horizons)),
                       [](const auto& entry) {
                           return std::pair<std::string, std::tuple<HostAndPort, int>>{
                               entry.first, {entry.second, entry.second.port()}};
                       });
        for (auto& horizon : this->reverseMapping) {
            // The Horizon for each reverse should always exist.
            invariant(horizons.count(horizon.second));
            std::get<0>(horizons[horizon.second]) = horizon.first;
        }
        horizons.erase(SplitHorizon::kDefaultHorizon);

        BSONObjBuilder horizonsBson(configBuilder.subobjStart("horizons"));
        for (const auto& horizon : horizons) {
            BSONObjBuilder horizonBson(horizonsBson.subobjStart(horizon.first));
            horizonBson.append("match", std::get<0>(horizon.second).toString());
            if (std::get<0>(horizon.second).port() != std::get<1>(horizon.second)) {
                horizonBson.append("replyPort", std::get<1>(horizon.second));
            }
        }
    }
}

SplitHorizon::SplitHorizon(const HostAndPort& host,
                           const boost::optional<BSONElement>& horizonsElement) {
    this->forwardMapping.emplace(SplitHorizon::kDefaultHorizon, host);
    this->reverseMapping.emplace(host, SplitHorizon::kDefaultHorizon);

    if (!horizonsElement)
        return;

    using namespace std::literals::string_literals;
    std::size_t horizonCount = 0;
    using std::begin;
    using std::end;
    struct HorizonEntry {
        std::string horizonName;
        HostAndPort matchAddress;
        int responsePort;
    };
    auto convert = [&horizonCount](auto&& horizon) -> HorizonEntry {
        ++horizonCount;
        const auto horizonName = horizon.fieldName();

        if (horizon.type() != Object) {
            uasserted(ErrorCodes::TypeMismatch,
                      str::stream() << "horizons." << horizonName
                                    << " field has non-object value of type "
                                    << typeName(horizon.type()));
        }

        const auto& mappingField = horizon.Obj();
        const auto endpoint = [&] {
            HostAndPort host([&] {
                std::string rv;
                uassertStatusOK(bsonExtractStringField(mappingField, "match", &rv));
                return rv;
            }());
            return HostAndPort(host.host(), host.port());
        }();

        const int port = [&]() -> int {
            try {
                long long rv;
                uassertStatusOK(bsonExtractIntegerField(mappingField, "replyPort", &rv));

                if (rv < 1 || rv > 65535) {
                    uasserted(ErrorCodes::BadValue,
                              str::stream() << "Reply port out of range for horizon "
                                            << horizonName);
                }
                return static_cast<int>(rv);
            }
            // missing replyPort is fine.
            catch (const ExceptionFor<ErrorCodes::NoSuchKey>&) {
                return endpoint.port();
            }
        }();

        return HorizonEntry{horizonName, endpoint, port};
    };
    std::vector<HorizonEntry> horizonEntries;

    const auto& horizonsObject = horizonsElement->Obj();
    std::transform(
        begin(horizonsObject), end(horizonsObject), back_inserter(horizonEntries), convert);

    std::transform(begin(horizonEntries),
                   end(horizonEntries),
                   inserter(forwardMapping, end(forwardMapping)),
                   [](const auto& entry) {
                       using ReturnType = decltype(forwardMapping)::value_type;

                       // Bind the replyPort to the horizon name, to permit port mapping.
                       HostAndPort host(entry.matchAddress.host(), entry.responsePort);
                       return ReturnType{entry.horizonName, host};
                   });

    if (forwardMapping.size() != horizonCount + 1) {
        auto horizonNames = [&] {
            std::vector<std::string> rv = {std::string{kDefaultHorizon}};
            std::transform(begin(horizonEntries),
                           end(horizonEntries),
                           back_inserter(rv),
                           [](const auto& entry) { return entry.horizonName; });
            return rv;
        }();


        std::sort(begin(horizonNames), end(horizonNames));
        auto duplicate = std::adjacent_find(begin(horizonNames), end(horizonNames));
        if (*duplicate == SplitHorizon::kDefaultHorizon) {
            uasserted(ErrorCodes::BadValue,
                      "Horizon name \"" + SplitHorizon::kDefaultHorizon +
                          "\" is reserved for internal mongodb usage");
        }
        uasserted(ErrorCodes::BadValue, "Duplicate horizon name found \""s + *duplicate + "\".");
    }

    std::transform(begin(horizonEntries),
                   end(horizonEntries),
                   inserter(reverseMapping, end(reverseMapping)),
                   [](auto&& entry) {
                       using ReturnType = decltype(reverseMapping)::value_type;
                       return ReturnType{entry.matchAddress, entry.horizonName};
                   });

    if (forwardMapping.size() != reverseMapping.size()) {
        auto horizonMember = [&] {
            std::vector<HostAndPort> rv = {host};
            std::transform(begin(horizonEntries),
                           end(horizonEntries),
                           back_inserter(rv),
                           [](const auto& entry) { return entry.matchAddress; });
            return rv;
        }();

        std::sort(begin(horizonMember), end(horizonMember));
        auto duplicate = std::adjacent_find(begin(horizonMember), end(horizonMember));

        uasserted(ErrorCodes::BadValue,
                  "Duplicate horizon member found \""s + duplicate->toString() + "\".");
    }
}

}  // namespace repl
}  // namespace mongo
