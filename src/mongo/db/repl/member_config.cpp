/**
 *    Copyright (C) 2018-present MongoDB, Inc.
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

#include "mongo/db/repl/member_config.h"

#include <boost/algorithm/string.hpp>

#include "mongo/bson/util/bson_check.h"
#include "mongo/bson/util/bson_extract.h"
#include "mongo/db/jsobj.h"
#include "mongo/util/str.h"

namespace mongo {
namespace repl {

const std::string MemberConfig::kIdFieldName = "_id";
const std::string MemberConfig::kVotesFieldName = "votes";
const std::string MemberConfig::kPriorityFieldName = "priority";
const std::string MemberConfig::kHostFieldName = "host";
const std::string MemberConfig::kHiddenFieldName = "hidden";
const std::string MemberConfig::kSlaveDelayFieldName = "slaveDelay";
const std::string MemberConfig::kArbiterOnlyFieldName = "arbiterOnly";
const std::string MemberConfig::kBuildIndexesFieldName = "buildIndexes";
const std::string MemberConfig::kTagsFieldName = "tags";
const std::string MemberConfig::kHorizonsFieldName = "horizons";
const std::string MemberConfig::kInternalVoterTagName = "$voter";
const std::string MemberConfig::kInternalElectableTagName = "$electable";
const std::string MemberConfig::kInternalAllTagName = "$all";

namespace {
const std::string kLegalMemberConfigFieldNames[] = {MemberConfig::kIdFieldName,
                                                    MemberConfig::kVotesFieldName,
                                                    MemberConfig::kPriorityFieldName,
                                                    MemberConfig::kHostFieldName,
                                                    MemberConfig::kHiddenFieldName,
                                                    MemberConfig::kSlaveDelayFieldName,
                                                    MemberConfig::kArbiterOnlyFieldName,
                                                    MemberConfig::kBuildIndexesFieldName,
                                                    MemberConfig::kTagsFieldName,
                                                    MemberConfig::kHorizonsFieldName};

const int kVotesFieldDefault = 1;
const double kPriorityFieldDefault = 1.0;
const Seconds kSlaveDelayFieldDefault(0);
const bool kArbiterOnlyFieldDefault = false;
const bool kHiddenFieldDefault = false;
const bool kBuildIndexesFieldDefault = true;

const Seconds kMaxSlaveDelay(3600 * 24 * 366);

}  // namespace

MemberConfig::MemberConfig(const BSONObj& mcfg, ReplSetTagConfig* tagConfig) {
    uassertStatusOK(bsonCheckOnlyHasFields(
        "replica set member configuration", mcfg, kLegalMemberConfigFieldNames));

    //
    // Parse _id field.
    //
    BSONElement idElement = mcfg[kIdFieldName];
    if (idElement.eoo())
        uasserted(ErrorCodes::NoSuchKey, str::stream() << kIdFieldName << " field is missing");

    if (!idElement.isNumber())
        uasserted(ErrorCodes::TypeMismatch,
                  str::stream() << kIdFieldName << " field has non-numeric type "
                                << typeName(idElement.type()));
    _id = idElement.numberInt();

    //
    // Parse h field.
    //
    std::string hostAndPortString;
    uassertStatusOK(bsonExtractStringField(mcfg, kHostFieldName, &hostAndPortString));
    boost::trim(hostAndPortString);
    HostAndPort host;
    uassertStatusOK(host.initialize(hostAndPortString));
    if (!host.hasPort()) {
        // make port explicit even if default.
        host = HostAndPort(host.host(), host.port());
    }

    this->_horizonForward.emplace(SplitHorizon::kDefaultHorizon, host);
    this->_horizonReverse.emplace(host, SplitHorizon::kDefaultHorizon);

    //
    // Parse votes field.
    //
    BSONElement votesElement = mcfg[kVotesFieldName];
    if (votesElement.eoo()) {
        _votes = kVotesFieldDefault;
    } else if (votesElement.isNumber()) {
        _votes = votesElement.numberInt();
    } else {
        uasserted(ErrorCodes::TypeMismatch,
                  str::stream() << kVotesFieldName << " field value has non-numeric type "
                                << typeName(votesElement.type()));
    }

    //
    // Parse arbiterOnly field.
    //
    uassertStatusOK(bsonExtractBooleanFieldWithDefault(
        mcfg, kArbiterOnlyFieldName, kArbiterOnlyFieldDefault, &_arbiterOnly));

    //
    // Parse priority field.
    //
    BSONElement priorityElement = mcfg[kPriorityFieldName];
    if (priorityElement.eoo() ||
        (priorityElement.isNumber() && priorityElement.numberDouble() == kPriorityFieldDefault)) {
        _priority = _arbiterOnly ? 0.0 : kPriorityFieldDefault;
    } else if (priorityElement.isNumber()) {
        _priority = priorityElement.numberDouble();
    } else {
        uasserted(ErrorCodes::TypeMismatch,
                  str::stream() << kPriorityFieldName << " field has non-numeric type "
                                << typeName(priorityElement.type()));
    }

    //
    // Parse slaveDelay field.
    //
    BSONElement slaveDelayElement = mcfg[kSlaveDelayFieldName];
    if (slaveDelayElement.eoo()) {
        _slaveDelay = kSlaveDelayFieldDefault;
    } else if (slaveDelayElement.isNumber()) {
        _slaveDelay = Seconds(slaveDelayElement.numberInt());
    } else {
        uasserted(ErrorCodes::TypeMismatch,
                  str::stream() << kSlaveDelayFieldName << " field value has non-numeric type "
                                << typeName(slaveDelayElement.type()));
    }

    //
    // Parse hidden field.
    //
    uassertStatusOK(
        bsonExtractBooleanFieldWithDefault(mcfg, kHiddenFieldName, kHiddenFieldDefault, &_hidden));

    //
    // Parse buildIndexes field.
    //
    uassertStatusOK(bsonExtractBooleanFieldWithDefault(
        mcfg, kBuildIndexesFieldName, kBuildIndexesFieldDefault, &_buildIndexes));

    //
    // Parse "tags" field.
    //
    try {
        BSONElement tagsElement;
        uassertStatusOK(bsonExtractTypedField(mcfg, kTagsFieldName, Object, &tagsElement));
        for (auto&& tag : tagsElement.Obj()) {
            if (tag.type() != String) {
                uasserted(ErrorCodes::TypeMismatch,
                          str::stream() << "tags." << tag.fieldName()
                                        << " field has non-string value of type "
                                        << typeName(tag.type()));
            }
            _tags.push_back(tagConfig->makeTag(tag.fieldNameStringData(), tag.valueStringData()));
        }
    } catch (const ExceptionFor<ErrorCodes::NoSuchKey>&) {
        // No such key is okay in this case, everything else is a problem.
    }

    const auto horizonsElement = [&]() -> boost::optional<BSONElement> {
        BSONElement result;
        Status status = bsonExtractTypedField(mcfg, kHorizonsFieldName, Object, &result);
        if (!status.isOK()) {
            return boost::none;
        }
        return result;
    }();

    if (horizonsElement) {
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
                       inserter(_horizonForward, end(_horizonForward)),
                       [](const auto& entry) {
                           using ReturnType = decltype(_horizonForward)::value_type;

                           // Bind the replyPort to the horizon name, to permit port mapping.
                           HostAndPort host(entry.matchAddress.host(), entry.responsePort);
                           return ReturnType{entry.horizonName, host};
                       });

        if (_horizonForward.size() != horizonCount + 1) {
            auto horizonNames = [&] {
                std::vector<std::string> rv = {"__default"};
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
            uasserted(ErrorCodes::BadValue,
                      "Duplicate horizon name found \""s + *duplicate + "\".");
        }

        std::transform(begin(horizonEntries),
                       end(horizonEntries),
                       inserter(_horizonReverse, end(_horizonReverse)),
                       [](auto&& entry) {
                           using ReturnType = decltype(_horizonReverse)::value_type;
                           return ReturnType{entry.matchAddress, entry.horizonName};
                       });

        if (_horizonForward.size() != _horizonReverse.size()) {
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

    //
    // Add internal tags based on other member properties.
    //

    // Add a voter tag if this non-arbiter member votes; use _id for uniquity.
    const std::string id = str::stream() << _id;
    if (isVoter() && !_arbiterOnly) {
        _tags.push_back(tagConfig->makeTag(kInternalVoterTagName, id));
    }

    // Add an electable tag if this member is electable.
    if (isElectable()) {
        _tags.push_back(tagConfig->makeTag(kInternalElectableTagName, id));
    }

    // Add a tag for generic counting of this node.
    if (!_arbiterOnly) {
        _tags.push_back(tagConfig->makeTag(kInternalAllTagName, id));
    }
}

Status MemberConfig::validate() const {
    if (_id < 0 || _id > 255) {
        return Status(ErrorCodes::BadValue,
                      str::stream() << kIdFieldName << " field value of " << _id
                                    << " is out of range.");
    }

    if (_priority < 0 || _priority > 1000) {
        return Status(ErrorCodes::BadValue,
                      str::stream() << kPriorityFieldName << " field value of " << _priority
                                    << " is out of range");
    }
    if (_votes != 0 && _votes != 1) {
        return Status(ErrorCodes::BadValue,
                      str::stream() << kVotesFieldName << " field value is " << _votes
                                    << " but must be 0 or 1");
    }
    if (_arbiterOnly) {
        if (!_tags.empty()) {
            return Status(ErrorCodes::BadValue, "Cannot set tags on arbiters.");
        }
        if (!isVoter()) {
            return Status(ErrorCodes::BadValue, "Arbiter must vote (cannot have 0 votes)");
        }
    }
    if (_slaveDelay < Seconds(0) || _slaveDelay > kMaxSlaveDelay) {
        return Status(ErrorCodes::BadValue,
                      str::stream() << kSlaveDelayFieldName << " field value of "
                                    << durationCount<Seconds>(_slaveDelay)
                                    << " seconds is out of range");
    }
    // Check for additional electable requirements, when priority is non zero
    if (_priority != 0) {
        if (_votes == 0) {
            return Status(ErrorCodes::BadValue, "priority must be 0 when non-voting (votes:0)");
        }
        if (_slaveDelay > Seconds(0)) {
            return Status(ErrorCodes::BadValue, "priority must be 0 when slaveDelay is used");
        }
        if (_hidden) {
            return Status(ErrorCodes::BadValue, "priority must be 0 when hidden=true");
        }
        if (!_buildIndexes) {
            return Status(ErrorCodes::BadValue, "priority must be 0 when buildIndexes=false");
        }
    }
    return Status::OK();
}

bool MemberConfig::hasTags(const ReplSetTagConfig& tagConfig) const {
    for (std::vector<ReplSetTag>::const_iterator tag = _tags.begin(); tag != _tags.end(); tag++) {
        std::string tagKey = tagConfig.getTagKey(*tag);
        if (tagKey[0] == '$') {
            // Filter out internal tags
            continue;
        }
        return true;
    }
    return false;
}

BSONObj MemberConfig::toBSON(const ReplSetTagConfig& tagConfig) const {
    BSONObjBuilder configBuilder;
    configBuilder.append("_id", _id);
    configBuilder.append("host", _host().toString());
    configBuilder.append("arbiterOnly", _arbiterOnly);
    configBuilder.append("buildIndexes", _buildIndexes);
    configBuilder.append("hidden", _hidden);
    configBuilder.append("priority", _priority);

    BSONObjBuilder tags(configBuilder.subobjStart("tags"));
    for (std::vector<ReplSetTag>::const_iterator tag = _tags.begin(); tag != _tags.end(); tag++) {
        std::string tagKey = tagConfig.getTagKey(*tag);
        if (tagKey[0] == '$') {
            // Filter out internal tags
            continue;
        }
        tags.append(tagKey, tagConfig.getTagValue(*tag));
    }
    tags.done();

    // `_horizonForward` should always contain the "__default" horizon, so we need to emit the
    // horizon repl specification when there are OTHER horizons.
    if (_horizonForward.size() > 1) {
        StringMap<std::tuple<HostAndPort, int>> horizons;
        std::transform(begin(_horizonForward),
                       end(_horizonForward),
                       inserter(horizons, end(horizons)),
                       [](const auto& entry) {
                           return std::pair<std::string, std::tuple<HostAndPort, int>>{
                               entry.first, {entry.second, entry.second.port()}};
                       });
        for (auto& horizon : _horizonReverse) {
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

    configBuilder.append("slaveDelay", durationCount<Seconds>(_slaveDelay));
    configBuilder.append("votes", getNumVotes());
    return configBuilder.obj();
}

}  // namespace repl
}  // namespace mongo
