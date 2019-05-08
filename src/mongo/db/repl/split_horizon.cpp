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

using namespace std::literals::string_literals;

using std::begin;
using std::end;

namespace mongo {
namespace repl {
namespace {
const auto getSplitHorizonParameters = Client::declareDecoration<SplitHorizon::Parameters>();
}  // namespace

void SplitHorizon::setParameters(Client* const client,
                                 boost::optional<std::string> sniName,
                                 boost::optional<HostAndPort> connectionTarget) {
    stdx::lock_guard<Client> lk(*client);
    getSplitHorizonParameters(*client) = {std::move(sniName), std::move(connectionTarget)};
}

auto SplitHorizon::getParameters(const Client* const client) -> Parameters {
    return getSplitHorizonParameters(*client);
}

StringData SplitHorizon::determineHorizon(const SplitHorizon::Parameters& horizonParameters) const {
    if (horizonParameters.connectionTarget) {
        const auto& connectionTarget = *horizonParameters.connectionTarget;
        auto found = reverseMapping.find(connectionTarget);
        if (found != end(reverseMapping))
            return found->second;
    } else if (horizonParameters.sniName) {
        const auto sniName = *horizonParameters.sniName;
        auto found = reverseHostMapping.find(sniName);
        if (found != end(reverseHostMapping)) {
            if (!found->second) {
                const auto message = "Attempt to lookup a multi-port mapping by SNI name only (" +
                    sniName + ")-- legacy driver use in unsupported situation detected.";
                log() << message;
                uasserted(ErrorCodes::HostNotFound, message);
            }
            return *found->second;
        }
    }
    return kDefaultHorizon;
}

void SplitHorizon::toBSON(const ReplSetTagConfig& tagConfig, BSONObjBuilder& configBuilder) const {
    invariant(!forwardMapping.empty());
    invariant(forwardMapping.count(SplitHorizon::kDefaultHorizon));

    // `forwardMapping` should always contain the "__default" horizon, so we need to emit the
    // horizon repl specification only when there are OTHER horizons besides it.  If there's only
    // one horizon, it's gotta be "__default", so we do nothing.
    if (forwardMapping.size() == 1)
        return;

    BSONObjBuilder horizonsBson(configBuilder.subobjStart("horizons"));
    for (const auto& horizon : forwardMapping) {
        if (horizon.first == SplitHorizon::kDefaultHorizon)
            continue;
        horizonsBson.append(horizon.first, horizon.second.toString());
    }
}

namespace {
using AllMappings = std::tuple<SplitHorizon::ForwardMapping,
                               SplitHorizon::ReverseMapping,
                               SplitHorizon::ReverseHostOnlyMapping>;

// The reverse mappings for a forward mapping are used to fully initialize a `SplitHorizon`
// instance.
AllMappings computeReverseMappings(SplitHorizon::ForwardMapping forwardMapping) {
    using ForwardMapping = SplitHorizon::ForwardMapping;
    using ReverseMapping = SplitHorizon::ReverseMapping;
    using ReverseHostOnlyMapping = SplitHorizon::ReverseHostOnlyMapping;

    // Build the reverse mapping (from `HostAndPort` to horizon names) from the forward mapping
    // table.
    ReverseMapping reverseMapping;
    std::transform(begin(forwardMapping),
                   end(forwardMapping),
                   inserter(reverseMapping, end(reverseMapping)),
                   [](auto&& entry) {
                       using ReturnType = decltype(reverseMapping)::value_type;
                       return ReturnType{entry.second, entry.first};
                   });


    // Check for duplicate host-and-port entries.
    if (forwardMapping.size() != reverseMapping.size()) {
        const auto horizonMember = [&] {
            std::vector<HostAndPort> rv;
            std::transform(begin(forwardMapping),
                           end(forwardMapping),
                           back_inserter(rv),
                           [](const auto& entry) { return entry.second; });
            std::sort(begin(rv), end(rv));
            return rv;
        }();

        auto duplicate = std::adjacent_find(begin(horizonMember), end(horizonMember));
        invariant(duplicate != end(horizonMember));

        uasserted(ErrorCodes::BadValue,
                  "Duplicate horizon member found \""s + duplicate->toString() + "\".");
    }

    // Build the reverse mapping (from host-only to horizon names) from the forward mapping table.
    ReverseHostOnlyMapping reverseHostMapping;

    // Default horizon case is special -- it always has to exist, and needs to be set before
    // entering the loop, to correctly handle ambiguous host-only cases within that horizon.
    reverseHostMapping.emplace(forwardMapping[SplitHorizon::kDefaultHorizon].host(),
                               std::string{SplitHorizon::kDefaultHorizon});
    for (const auto& entry : forwardMapping) {
        // If a host appears more than once, by name, disable SNI lookup for it, as it indicates
        // multi-port scenarios.  In those cases, duplicate ports will be found by the detection
        // code for the full reverse mapping.
        if (reverseHostMapping.count(entry.second.host())) {
            // However, if the repeated host is the default horizon's host, this configuration is
            // not disabled, but legacy connections will not be able to avail themselves of the
            // other horizons -- they will appear to be `"__default"`.
            if (reverseHostMapping[entry.second.host()] == SplitHorizon::kDefaultHorizon.toString())
                continue;

            // When we can't disambiguate on reverse lookup by host alone, disable that host for SNI
            // lookup into horizon name.
            reverseHostMapping[entry.second.host()] = boost::none;
        } else {
            // Otherwise, just preserve the mapping.
            reverseHostMapping[entry.second.host()] = entry.first;
        }
    }

    return {std::move(forwardMapping), std::move(reverseMapping), std::move(reverseHostMapping)};
}
}  // namespace

// A split horizon build from a known forward mapping table should just need to construct the
// reverse mappings.
SplitHorizon::SplitHorizon(ForwardMapping mapping)
    : SplitHorizon(computeReverseMappings(std::move(mapping))) {}

namespace {
SplitHorizon::ForwardMapping computeForwardMappings(
    const HostAndPort& host, const boost::optional<BSONElement>& horizonsElement) {
    SplitHorizon::ForwardMapping forwardMapping;

    if (horizonsElement) {
        using MapMember = std::pair<std::string, HostAndPort>;

        // Process all of the BSON description of horizons into a linear list.
        auto convert = [](auto&& horizonObj) -> MapMember {
            const StringData horizonName = horizonObj.fieldName();

            if (horizonObj.type() != String) {
                uasserted(ErrorCodes::TypeMismatch,
                          str::stream() << "horizons." << horizonName
                                        << " field has non-string value of type "
                                        << typeName(horizonObj.type()));
            } else if (horizonName == SplitHorizon::kDefaultHorizon) {
                uasserted(ErrorCodes::BadValue,
                          "Horizon name \"" + SplitHorizon::kDefaultHorizon +
                              "\" is reserved for internal mongodb usage");
            } else if (horizonName == "") {
                uasserted(ErrorCodes::BadValue, "Horizons cannot have empty names");
            }

            return {horizonName.toString(), HostAndPort{horizonObj.valueStringData()}};
        };

        const auto& horizonsObject = horizonsElement->Obj();
        const auto horizonEntries = [&] {
            std::vector<MapMember> rv;
            std::transform(
                begin(horizonsObject), end(horizonsObject), inserter(rv, end(rv)), convert);
            return rv;
        }();


        // Dump the linear list into the forward mapping.
        forwardMapping.insert(begin(horizonEntries), end(horizonEntries));

        // Check for duplicate horizon names and reserved names, which would be if the horizon
        // linear list size disagrees with the size of the mapping.
        if (horizonEntries.size() != forwardMapping.size()) {
            // If the map has a different amount than a linear list of the bson converted, then it
            // had better be a lesser amount, indicating duplicates.  A greater amount should be
            // impossible.
            invariant(horizonEntries.size() > forwardMapping.size());

            // Find which one is duplicated.
            const auto horizonNames = [&] {
                std::vector<std::string> rv;
                std::transform(begin(horizonEntries),
                               end(horizonEntries),
                               back_inserter(rv),
                               [](const auto& entry) { return entry.first; });
                std::sort(begin(rv), end(rv));
                return rv;
            }();

            const auto duplicate = std::adjacent_find(begin(horizonNames), end(horizonNames));

            // Report our duplicate if found.
            if (duplicate != end(horizonNames)) {
                uasserted(ErrorCodes::BadValue,
                          "Duplicate horizon name found \""s + *duplicate + "\".");
            }
        }
    }

    // Finally add the default mapping, regardless of whether we processed a configuration.
    const bool successInDefaultPlacement =
        forwardMapping.emplace(SplitHorizon::kDefaultHorizon, host).second;
    // And that insertion BETTER succeed -- it mightn't if there's a bug in the configuration
    // processing logic.
    invariant(successInDefaultPlacement);

    return forwardMapping;
}
}  // namespace

// A split horizon constructed from the BSON configuration and the host specifier for this member
// needs to compute the forward mapping table.  In turn that will be used to compute the reverse
// mapping table.
SplitHorizon::SplitHorizon(const HostAndPort& host,
                           const boost::optional<BSONElement>& horizonsElement)
    : SplitHorizon(computeForwardMappings(host, horizonsElement)) {}
#if 0
{
    forwardMapping.emplace(SplitHorizon::kDefaultHorizon, host);
    reverseMapping.emplace(host, SplitHorizon::kDefaultHorizon);
    reverseHostMapping.emplace(host.host(), std::string{SplitHorizon::kDefaultHorizon});

    if (!horizonsElement)
        return;

    using namespace std::literals::string_literals;
    std::size_t horizonCount = 0;
    using std::begin;
    using std::end;
    struct HorizonEntry {
        std::string horizonName;
        HostAndPort target;
    };
    auto convert = [&horizonCount](auto&& horizon) -> HorizonEntry {
        ++horizonCount;
        const auto horizonName = horizon.fieldName();

        if (horizon.type() != String) {
            uasserted(ErrorCodes::TypeMismatch,
                      str::stream() << "horizons." << horizonName
                                    << " field has non-string value of type "
                                    << typeName(horizon.type()));
        }

        return HorizonEntry{ horizonName, HostAndPort{horizon.valueStringData()}};
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

                       return ReturnType{entry.horizonName, entry.target};
                   });

    // Check for duplicate horizon names and reserved names.
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

    // Build the reverse mapping (from `HostAndPort` to horizon names) from the forward mapping
    // table.
    std::transform(begin(horizonEntries),
                   end(horizonEntries),
                   inserter(reverseMapping, end(reverseMapping)),
                   [](auto&& entry) {
                       using ReturnType = decltype(reverseMapping)::value_type;
                       return ReturnType{entry.target, entry.horizonName};
                   });


    // Check for duplicate host-and-port entries.
    if (forwardMapping.size() != reverseMapping.size()) {
        auto horizonMember = [&] {
            std::vector<HostAndPort> rv = {host};
            std::transform(begin(horizonEntries),
                           end(horizonEntries),
                           back_inserter(rv),
                           [](const auto& entry) { return entry.target; });
            return rv;
        }();

        std::sort(begin(horizonMember), end(horizonMember));
        auto duplicate = std::adjacent_find(begin(horizonMember), end(horizonMember));

        uasserted(ErrorCodes::BadValue,
                  "Duplicate horizon member found \""s + duplicate->toString() + "\".");
    }

    // Build the reverse mapping (from host-only to horizon names) from the forward mapping table.
    for (const auto& entry : horizonEntries) {
        // If a host appears more than once, disable SNI lookup for it.
        if (reverseHostMapping.count(entry.target.host())) {
            // However, if the repeated host is the default horizon's host, this configuration is
            // okay, but legacy connections will not be able to avail themselves of the other
            // horizons -- they will appear to be `"__default"`.
            if (reverseHostMapping[entry.target.host()] == std::string(SplitHorizon::kDefaultHorizon))
                continue;

            reverseHostMapping[entry.target.host()] = boost::none;
        }
        // Otherwise, just preserve the mapping.
        else
            reverseHostMapping[entry.target.host()] = entry.horizonName;
    }
}
#endif

}  // namespace repl
}  // namespace mongo
