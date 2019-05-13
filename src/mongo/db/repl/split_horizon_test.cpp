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
static const std::string defaultPort = "4242";
static const std::string defaultHostAndPort = defaultHost + ":" + defaultPort;

static const std::string matchingHost = "matching.dns.name.example.com";
static const std::string matchingPort = "4243";
static const std::string matchingHostAndPort = matchingHost + ":" + matchingPort;


static const std::string nonmatchingHost = "nonmatching.dns.name.example.com";
static const std::string nonmatchingPort = "4244";
static const std::string nonmatchingHostAndPort = nonmatchingHost + ":" + nonmatchingPort;

static const std::string altPort = ":666";

TEST(SplitHorizonTesting, determineHorizon) {
    struct Input {
        SplitHorizon::ForwardMapping forwardMapping;  // Will get "__default" added to it.
        SplitHorizon::Parameters horizonParameters;
        using MappingType = std::map<std::string, std::string>;


        Input(const MappingType& mapping, boost::optional<std::string> sniName)
            : horizonParameters(std::move(sniName)) {
            forwardMapping.emplace(SplitHorizon::kDefaultHorizon, defaultHostAndPort);

            using ForwardMappingValueType = decltype(forwardMapping)::value_type;
            using ElementType = MappingType::value_type;
            auto createForwardMapping = [](const ElementType& element) {
                return ForwardMappingValueType{element.first, HostAndPort(element.second)};
            };
            std::transform(begin(mapping),
                           end(mapping),
                           inserter(forwardMapping, end(forwardMapping)),
                           createForwardMapping);
        }
    };

    struct {
        const int lineNumber;
        Input input;

        std::string expected;
    } tests[] = {
        // No parameters and no horizon views configured.
        {__LINE__, {{}, boost::none}, "__default"},
        {__LINE__, {{}, defaultHost}, "__default"},

        // No SNI -> no match
        {__LINE__, {{{"unusedHorizon", "badmatch:00001"}}, boost::none}, "__default"},

        // Unmatching SNI -> no match
        {__LINE__, {{{"unusedHorizon", "badmatch:00001"}}, nonmatchingHost}, "__default"},

        // Matching SNI -> match
        {__LINE__, {{{"targetHorizon", matchingHostAndPort}}, matchingHost}, "targetHorizon"},
    };

    for (const auto& test : tests) {
        const auto& expected = test.expected;
        const auto& input = test.input;

        const std::string witness =
            SplitHorizon(input.forwardMapping).determineHorizon(input.horizonParameters).toString();
        const bool equals = (witness == expected);

        if (!equals)
            std::cerr << "Failing test input from line: " << test.lineNumber << std::endl;
        ASSERT_EQUALS(witness, expected);
    }

    const Input failingCases[] = {};

    for (const auto& input : failingCases) {
        SplitHorizon horizon(input.forwardMapping);

        ASSERT_THROWS(horizon.determineHorizon(input.horizonParameters),
                      ExceptionFor<ErrorCodes::HostNotFound>);
    }

    const Input failingCtorCases[] = {
        // Matching SNI, multiPort, collision -> match
        {{{"targetHorizon", matchingHost + altPort}, {"badHorizon", matchingHostAndPort}},
         matchingHost},

        // Default horizon ambiguous case is a failure
        {{{"targetHorizon", defaultHost + altPort}, {"badHorizon", nonmatchingHostAndPort}},
         defaultHost},
    };

    for (const auto& input : failingCases) {
        ASSERT_THROWS(SplitHorizon(input.forwardMapping), ExceptionFor<ErrorCodes::BadValue>);
    }
}

TEST(SplitHorizonTesting, basicConstruction) {
    struct Input {
        SplitHorizon::ForwardMapping forwardMapping;  // Will get "__default" added to it.
        using MappingType = std::map<std::string, std::string>;


        Input(const MappingType& mapping) {
            forwardMapping.emplace(SplitHorizon::kDefaultHorizon, defaultHostAndPort);

            using ForwardMappingValueType = decltype(forwardMapping)::value_type;
            using ElementType = MappingType::value_type;
            auto createForwardMapping = [](const ElementType& element) {
                return ForwardMappingValueType{element.first, HostAndPort(element.second)};
            };
            std::transform(begin(mapping),
                           end(mapping),
                           inserter(forwardMapping, end(forwardMapping)),
                           createForwardMapping);
        }
    };

    const struct {
        Input input;
        ErrorCodes::Error expectedErrorCode;
    } tests[] = {{{{}}, ErrorCodes::OK},
                 {{{{"extraHorizon", "example.com:42"}}}, ErrorCodes::OK},
                 {{{{"extraHorizon", "example.com:42"}, {"extraHorizon2", "extra.example.com:42"}}},
                  ErrorCodes::OK}};
    for (const auto& test : tests) {
        const auto& input = test.input;
        const auto& expectedErrorCode = test.expectedErrorCode;
        const auto horizonOpt = [&]() -> boost::optional<SplitHorizon> {
            try {
                return SplitHorizon(input.forwardMapping);
            } catch (const DBException& ex) {
                ASSERT_EQUALS(ex.toStatus().code(), expectedErrorCode);
                return boost::none;
            }
        }();

        if (!horizonOpt)
            continue;

        const auto& horizon = *horizonOpt;
        for (const auto& element : input.forwardMapping) {
            const auto found = horizon.getForwardMappings().find(element.first);
            ASSERT_TRUE(found != end(horizon.getForwardMappings()));
            ASSERT_EQUALS(HostAndPort(element.second).toString(), found->second.toString());
        }
        ASSERT_EQUALS(input.forwardMapping.size(), horizon.getForwardMappings().size());
        ASSERT_EQUALS(input.forwardMapping.size(), horizon.getReverseHostMappings().size());
    }
}

TEST(SplitHorizonTesting, BSONConstruction) {}

TEST(SplitHorizonTesting, toBSON) {}
}  // namespace
}  // namespace repl
}  // namespace mongo
