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

#include "mongo/platform/basic.h"

#include "mongo/db/repl/split_horizon.h"

#include <algorithm>
#include <boost/optional.hpp>
#include <iterator>

#include "mongo/stdx/utility.h"
#include "mongo/unittest/unittest.h"

using namespace std::literals::string_literals;

namespace mongo {
namespace repl {
namespace {
static const std::string defaultHost = "default.dns.name.example.com";
TEST(SplitHorizonTesting, determineHorizon) {

    struct {
        struct Input {
            int port;
            SplitHorizon::ForwardMapping forwardMapping;  // Will get "__default" added to it.
            SplitHorizon::ReverseMapping reverseMapping;
            SplitHorizon::Parameters horizonParameters;

            Input(const int p,
                  const std::map<std::string, std::string>& mapping,
                  SplitHorizon::Parameters params)
                : port(p), horizonParameters(std::move(params)) {
                forwardMapping.emplace(SplitHorizon::defaultHorizon, defaultHost + ":4242");

                auto createForwardMapping =
                    [](const auto& element) -> decltype(forwardMapping)::value_type {
                    return {element.first, HostAndPort(element.second)};
                };
                std::transform(begin(mapping),
                               end(mapping),
                               inserter(forwardMapping, end(forwardMapping)),
                               createForwardMapping);

                auto createReverseMapping =
                    [](const auto& element) -> decltype(reverseMapping)::value_type {
                    return {element.second, element.first};
                };
                std::transform(begin(stdx::as_const(forwardMapping)),
                               end(stdx::as_const(forwardMapping)),
                               inserter(reverseMapping, end(reverseMapping)),
                               createReverseMapping);
            }
        } input;

        std::string expected;
    } tests[] = {
        // No parameters and no horizon views configured.
        {{4242, {}, {}}, "__default"},
        {{4242, {}, {"SomeApplication", boost::none, boost::none, boost::none}}, "__default"},
        {{4242, {}, {"SomeApplication", defaultHost, boost::none, boost::none}}, "__default"},

        // No SNI, no match
        {{4242,
          {{"unusedHorizon", "badmatch:00001"}},
          {"SomeApplication", boost::none, boost::none, boost::none}},
         "__default"},

        // Has SNI, no match
        {{4242,
          {{"unusedHorizon", "badmatch:00001"}},
          {"SomeApplication", defaultHost, boost::none, boost::none}},
         "__default"},

        // Application Name passed, but doesn't match.  SNI Name
        {{4242,
          {{"matchingHorizon", "matchingTarget:4242"}},
          {"SomeApplication", "matchingTarget"s, boost::none, boost::none}},
         "matchingHorizon"},

#ifdef MONGO_ENABLE_SPLIT_HORIZON_APPNAME
        // Application Name matches, but nothing else passed.
        {{4242,
          {{"matchingHorizon", "matchingTarget:4242"}},
          {"matchingHorizon", boost::none, boost::none, boost::none}},
         "matchingHorizon"},
#endif
    };

    for (const auto& test : tests) {
        const auto& expected = test.expected;
        const auto& input = test.input;
        const std::string witness =
            SplitHorizon::determineHorizon(
                input.port, input.forwardMapping, input.reverseMapping, input.horizonParameters)
                .toString();
        ASSERT_EQUALS(witness, expected);
    }
}
}  // namespace
}  // namespace repl
}  // namespace mongo
