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

#include "mongo/util/log.h"
#include "mongo/db/client.h"

namespace mongo
{
    namespace repl
    {
        namespace
        {
            const auto getSplitHorizonParameters= Client::declareDecoration<
                    SplitHorizon::Parameters >();
        }// namespace

        void
        SplitHorizon::setParameters( Client *const client, std::string appName,
                boost::optional<std::string> sniName,
                boost::optional<std::string> connectionTarget,
                boost::optional<std::string> explicitHorizonName )
        {
            stdx::lock_guard<Client> lk(*client);
            getSplitHorizonParameters( *client )= { std::move( appName ), std::move( sniName ),
                    std::move( connectionTarget ), std::move( explicitHorizonName ) };
        }

        auto
        SplitHorizon::getParameters( const Client *const client )
            -> Parameters
        {
            return getSplitHorizonParameters( *client );
        }

        StringData SplitHorizon::determineHorizon(
            const int incomingPort,
            const ForwardMapping& forwardMapping,
            const ReverseMapping& reverseMapping,
            const SplitHorizon::Parameters& horizonParameters) {
            log() << "Mapping horizon with SNI name: " << horizonParameters.sniName.value_or( "<NONE>" );
            if (horizonParameters.explicitHorizonName) {
                // Unlike `appName`, the explicit horizon request isn't checked for validity against a
                // fallback; therefore failure to select a valid horizon name explicitly will lead to
                // command failure.
                log() << "Explicit Horizon Name case";
                return *horizonParameters.explicitHorizonName;
            } else if (horizonParameters.connectionTarget) {
                log() << "Connection target case";
                const HostAndPort connectionTarget(*horizonParameters.connectionTarget);
                auto found = reverseMapping.find(connectionTarget);
                if (found != end(reverseMapping))
                    return found->second;
            } else if (horizonParameters.sniName) {
                log() << "SNI Name match case";
                const HostAndPort connectionTarget(*horizonParameters.sniName, incomingPort);
                auto found = reverseMapping.find(connectionTarget);
                if (found != end(reverseMapping))
                    return found->second;
            }
        #ifdef MONGO_ENABLE_SPLIT_HORIZON_APPNAME
            else if (forwardMapping.count(horizonParameters.appName)) {
                log() << "AppName case";
                return horizonParameters.appName;
            }
        #endif
            log() << "Fallthrough case";
            return defaultHorizon;
        }
    }//namespace repl
}//namespace mongo
