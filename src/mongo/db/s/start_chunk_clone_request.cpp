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

#include "mongo/db/s/start_chunk_clone_request.h"

#include "mongo/base/status_with.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/util/bson_extract.h"

namespace mongo {
namespace {

const char kRecvChunkStart[] = "_recvChunkStart";
const char kFromShardConnectionString[] = "from";
const char kFromShardId[] = "fromShardName";
const char kToShardId[] = "toShardName";
const char kChunkMinKey[] = "min";
const char kChunkMaxKey[] = "max";
const char kShardKeyPattern[] = "shardKeyPattern";

}  // namespace

StartChunkCloneRequest::StartChunkCloneRequest(NamespaceString nss,
                                               MigrationSessionId sessionId,
                                               MigrationSecondaryThrottleOptions secondaryThrottle)
    : _nss(std::move(nss)),
      _sessionId(std::move(sessionId)),
      _secondaryThrottle(std::move(secondaryThrottle)) {}

StartChunkCloneRequest StartChunkCloneRequest::createFromCommand(NamespaceString nss,
                                                                 const BSONObj& obj) {
    auto secondaryThrottle =
        uassertStatusOK(MigrationSecondaryThrottleOptions::createFromCommand(obj));

    auto sessionId = uassertStatusOK(MigrationSessionId::extractFromBSON(obj));

    StartChunkCloneRequest request(
        std::move(nss), std::move(sessionId), std::move(secondaryThrottle));

    {
        std::string fromShardConnectionString;
        uassertStatusOK(
            bsonExtractStringField(obj, kFromShardConnectionString, &fromShardConnectionString));

        request._fromShardCS = uassertStatusOK(ConnectionString::parse(fromShardConnectionString));
    }

    {
        std::string fromShard;
        uassertStatusOK(bsonExtractStringField(obj, kFromShardId, &fromShard));
        request._fromShardId = std::move(fromShard);
    }

    {
        std::string toShard;
        uassertStatusOK(bsonExtractStringField(obj, kToShardId, &toShard));
        request._toShardId = std::move(toShard);
    }

    {
        BSONElement elem;
        uassertStatusOK(bsonExtractTypedField(obj, kChunkMinKey, BSONType::Object, &elem));

        request._minKey = elem.Obj().getOwned();

        if (request._minKey.isEmpty()) {
            uasserted(ErrorCodes::UnsupportedFormat, "The chunk min key cannot be empty");
        }
    }

    {
        BSONElement elem;
        uassertStatusOK(bsonExtractTypedField(obj, kChunkMaxKey, BSONType::Object, &elem));

        request._maxKey = elem.Obj().getOwned();

        if (request._maxKey.isEmpty()) {
            uasserted(ErrorCodes::UnsupportedFormat, "The chunk max key cannot be empty");
        }
    }

    {
        BSONElement elem;
        uassertStatusOK(bsonExtractTypedField(obj, kShardKeyPattern, BSONType::Object, &elem));

        request._shardKeyPattern = elem.Obj().getOwned();

        if (request._shardKeyPattern.isEmpty()) {
            uasserted(ErrorCodes::UnsupportedFormat, "The shard key pattern cannot be empty");
        }
    }

    return request;
}

void StartChunkCloneRequest::appendAsCommand(
    BSONObjBuilder* builder,
    const NamespaceString& nss,
    const MigrationSessionId& sessionId,
    const ConnectionString& fromShardConnectionString,
    const ShardId& fromShardId,
    const ShardId& toShardId,
    const BSONObj& chunkMinKey,
    const BSONObj& chunkMaxKey,
    const BSONObj& shardKeyPattern,
    const MigrationSecondaryThrottleOptions& secondaryThrottle) {
    invariant(builder->asTempObj().isEmpty());
    invariant(nss.isValid());
    invariant(fromShardConnectionString.isValid());

    builder->append(kRecvChunkStart, nss.ns());
    sessionId.append(builder);
    builder->append(kFromShardConnectionString, fromShardConnectionString.toString());
    builder->append(kFromShardId, fromShardId.toString());
    builder->append(kToShardId, toShardId.toString());
    builder->append(kChunkMinKey, chunkMinKey);
    builder->append(kChunkMaxKey, chunkMaxKey);
    builder->append(kShardKeyPattern, shardKeyPattern);
    secondaryThrottle.append(builder);
}

}  // namespace mongo
