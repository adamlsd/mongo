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

#pragma once

#include <map>
#include <string>

#include <boost/optional.hpp>

#include "mongo/base/string_data.h"
#include "mongo/db/client.h"
#include "mongo/db/repl/repl_set_tag.h"
#include "mongo/util/net/hostandport.h"

namespace mongo {
namespace repl {
class SplitHorizon {
public:
    static constexpr auto kDefaultHorizon = "__default"_sd;

    using ForwardMapping = StringMap<HostAndPort>;
    using ReverseMapping = std::map<HostAndPort, std::string>;
    using ReverseHostOnlyMapping = std::map<std::string, boost::optional<std::string>>;

    struct Parameters {
        boost::optional<std::string> sniName;
        boost::optional<HostAndPort> connectionTarget;

        Parameters() = default;
        Parameters(boost::optional<std::string> initialSniName,
                   boost::optional<HostAndPort> initialConnectionTarget)
            : sniName(std::move(initialSniName)),
              connectionTarget(std::move(initialConnectionTarget)) {}
    };

    /**
     * Set the split horizon connection parameters, for use by future `isMaster` commands.
     */
    static void setParameters(Client* client,
                              boost::optional<std::string> sniName,
                              boost::optional<HostAndPort> connectionTarget);

    /**
     * Get the client's SplitHorizonParameters object.
     */
    static Parameters getParameters(const Client*);

    explicit SplitHorizon() = default;
    explicit SplitHorizon(const HostAndPort& host,
                          const boost::optional<BSONElement>& horizonsElement);


    explicit SplitHorizon(ForwardMapping forward);

    StringData determineHorizon(const Parameters& horizonParameters) const;

    const HostAndPort& getHostAndPort(StringData horizon) const {
        invariant(!forwardMapping.empty());
        invariant(!horizon.empty());
        auto found = forwardMapping.find(horizon);
        if (found == end(forwardMapping))
            uasserted(ErrorCodes::NoSuchKey, str::stream() << "No horizon named " << horizon);
        return found->second;
    }

    const auto& getHorizonMappings() const {
        return forwardMapping;
    }

    const auto& getHorizonReverseMappings() const {
        return reverseMapping;
    }

    const auto& getHorizonReverseHostMappings() const {
        return reverseHostMapping;
    }

    void toBSON(BSONObjBuilder& configBuilder) const;

private:
    // For testing only
    explicit SplitHorizon(
        std::tuple<ForwardMapping, ReverseMapping, ReverseHostOnlyMapping> mappings)
        : forwardMapping(std::move(std::get<0>(mappings))),
          reverseMapping(std::move(std::get<1>(mappings))),
          reverseHostMapping(std::move(std::get<2>(mappings))) {}

    // Maps each horizon name to a network address for this replica set member
    ForwardMapping forwardMapping;

    // Maps each network address (`HostAndPort`) which this replica set member has to a horizon name
    // under which that address applies
    ReverseMapping reverseMapping;

    // Maps each hostname which this replica set member has to a horizon name under which that
    // address applies
    ReverseHostOnlyMapping reverseHostMapping;
};
}  // namespace repl
}  // namespace mongo
