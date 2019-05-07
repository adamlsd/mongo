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

    using ForwardMapping = StringMap<HostAndPort>;              // Contains reply port
    using ReverseMapping = std::map<HostAndPort, std::string>;  // Contains match port

    struct Parameters {
        boost::optional<std::string> connectionTarget;
    };

    /**
     * Set the split horizon connection parameters, for use by future is-master commands.
     */
    static void setParameters(Client* client, boost::optional<std::string> connectionTarget);

    /**
     * Get the client's SplitHorizonParameters object.
     */
    static Parameters getParameters(const Client*);

    explicit SplitHorizon() = default;
    explicit SplitHorizon(const HostAndPort& host,
                          const boost::optional<BSONElement>& horizonsElement);


    StringData determineHorizon(int incomingPort, const Parameters& horizonParameters) const;

    const HostAndPort& getHostAndPort(StringData horizon) const {
        invariant(!this->forwardMapping.empty());
        invariant(!horizon.empty());
        auto found = this->forwardMapping.find(horizon);
        if (found == end(this->forwardMapping))
            uasserted(ErrorCodes::NoSuchKey, str::stream() << "No horizon named " << horizon);
        return found->second;
    }

    const auto& getHorizonMappings() const {
        return this->forwardMapping;
    }
    const auto& getHorizonReverseMappings() const {
        return this->reverseMapping;
    }

    void toBSON(const ReplSetTagConfig& tagConfig, BSONObjBuilder& configBuilder) const;

private:
    ForwardMapping
        forwardMapping;  // Maps each horizon name to a network address for this replica set member
    ReverseMapping reverseMapping;  // Maps each network address which this replica set member has
                                    // to a horizon name under which that address applies
};
}  // namespace repl
}  // namespace mongo
