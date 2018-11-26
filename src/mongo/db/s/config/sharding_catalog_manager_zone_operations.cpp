
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kSharding

#include "mongo/platform/basic.h"

#include "mongo/db/s/config/sharding_catalog_manager.h"

#include "mongo/base/status_with.h"
#include "mongo/client/read_preference.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/s/balancer/balancer_policy.h"
#include "mongo/db/write_concern_options.h"
#include "mongo/s/catalog/sharding_catalog_client.h"
#include "mongo/s/catalog/type_collection.h"
#include "mongo/s/catalog/type_shard.h"
#include "mongo/s/catalog/type_tags.h"
#include "mongo/s/client/shard.h"
#include "mongo/s/client/shard_registry.h"
#include "mongo/s/grid.h"
#include "mongo/s/shard_key_pattern.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {

const ReadPreferenceSetting kConfigPrimarySelector(ReadPreference::PrimaryOnly);
const WriteConcernOptions kNoWaitWriteConcern(1, WriteConcernOptions::SyncMode::UNSET, Seconds(0));

/**
 * Checks if the given key range for the given namespace conflicts with an existing key range.
 * Note: range should have the full shard key.
 * Returns ErrorCodes::RangeOverlapConflict is an overlap is detected.
 */
void checkForOveralappedZonedKeyRange(OperationContext* opCtx,
                                      Shard* configServer,
                                      const NamespaceString& nss,
                                      const ChunkRange& range,
                                      const std::string& zoneName,
                                      const KeyPattern& shardKeyPattern) {
    DistributionStatus chunkDist(nss, ShardToChunksMap{});

    auto tag = uassertStatusOK(
        configServer->exhaustiveFindOnConfig(opCtx,
                                             kConfigPrimarySelector,
                                             repl::ReadConcernLevel::kLocalReadConcern,
                                             TagsType::ConfigNS,
                                             BSON(TagsType::ns(nss.ns())),
                                             BSONObj(),
                                             0));

    const auto& tagDocList = tag.docs;
    for (const auto& tagDoc : tagDocList) {
        const auto parsedTagDoc = uassertStatusOK(TagsType::fromBSON(tagDoc));

        // Always extend ranges to full shard key to be compatible with tags created before
        // the zone commands were implemented.
        uassertStatusOK(chunkDist.addRangeToZone(
            ZoneRange(shardKeyPattern.extendRangeBound(parsedTagDoc.getMinKey(), false),
                      shardKeyPattern.extendRangeBound(parsedTagDoc.getMaxKey(), false),
                      parsedTagDoc.getTag())));
    }

    uassertStatusOK(chunkDist.addRangeToZone(ZoneRange(range.getMin(), range.getMax(), zoneName)));
}

/**
 * Returns a new range based on the given range with the full shard key.
 * Returns:
 * - ErrorCodes::NamespaceNotSharded if nss is not sharded.
 * - ErrorCodes::ShardKeyNotFound if range is not compatible (for example, not a prefix of shard
 * key) with the shard key of nss.
 */
ChunkRange includeFullShardKey(OperationContext* opCtx,
                               Shard* configServer,
                               const NamespaceString& nss,
                               const ChunkRange& range,
                               KeyPattern* shardKeyPatternOut) {
    auto findColl = uassertStatusOK(
        configServer->exhaustiveFindOnConfig(opCtx,
                                             kConfigPrimarySelector,
                                             repl::ReadConcernLevel::kLocalReadConcern,
                                             CollectionType::ConfigNS,
                                             BSON(CollectionType::fullNs(nss.ns())),
                                             BSONObj(),
                                             1));

    const auto& findCollResult = findColl.docs;

    if (findCollResult.size() < 1) {
        uasserted(ErrorCodes::NamespaceNotSharded, str::stream() << nss.ns() << " is not sharded");
    }

    auto collDoc = uassertStatusOK(CollectionType::fromBSON(findCollResult.front()));
    if (collDoc.getDropped()) {
        uasserted(ErrorCodes::NamespaceNotSharded, str::stream() << nss.ns() << " is not sharded");
    }

    const auto& shardKeyPattern = collDoc.getKeyPattern();
    const auto& shardKeyBSON = shardKeyPattern.toBSON();
    *shardKeyPatternOut = shardKeyPattern;

    if (!range.getMin().isFieldNamePrefixOf(shardKeyBSON)) {
        uasserted(ErrorCodes::ShardKeyNotFound,
                  str::stream() << "min: " << range.getMin() << " is not a prefix of the shard key "
                                << shardKeyBSON
                                << " of ns: "
                                << nss.ns());
    }

    if (!range.getMax().isFieldNamePrefixOf(shardKeyBSON)) {
        uasserted(ErrorCodes::ShardKeyNotFound,
                  str::stream() << "max: " << range.getMax() << " is not a prefix of the shard key "
                                << shardKeyBSON
                                << " of ns: "
                                << nss.ns());
    }

    return ChunkRange(shardKeyPattern.extendRangeBound(range.getMin(), false),
                      shardKeyPattern.extendRangeBound(range.getMax(), false));
}

}  // namespace

void ShardingCatalogManager::addShardToZone(OperationContext* opCtx,
                                            const std::string& shardName,
                                            const std::string& zoneName) {
    Lock::ExclusiveLock lk(opCtx->lockState(), _kZoneOpLock);

    auto update = uassertStatusOK(Grid::get(opCtx)->catalogClient()->updateConfigDocument(
        opCtx,
        ShardType::ConfigNS,
        BSON(ShardType::name(shardName)),
        BSON("$addToSet" << BSON(ShardType::tags() << zoneName)),
        false,
        kNoWaitWriteConcern));

    if (!update) {
        uasserted(ErrorCodes::ShardNotFound,
                  str::stream() << "shard " << shardName << " does not exist");
    }
}

void ShardingCatalogManager::removeShardFromZone(OperationContext* opCtx,
                                                 const std::string& shardName,
                                                 const std::string& zoneName) {
    Lock::ExclusiveLock lk(opCtx->lockState(), _kZoneOpLock);

    auto configShard = Grid::get(opCtx)->shardRegistry()->getConfigShard();
    const NamespaceString shardNS(ShardType::ConfigNS);

    //
    // Check whether the shard even exist in the first place.
    //

    const bool shardExists = !uassertStatusOK(configShard->exhaustiveFindOnConfig(
                                                  opCtx,
                                                  kConfigPrimarySelector,
                                                  repl::ReadConcernLevel::kLocalReadConcern,
                                                  shardNS,
                                                  BSON(ShardType::name() << shardName),
                                                  BSONObj(),
                                                  1))
                                  .docs.empty();

    if (!shardExists) {
        uasserted(ErrorCodes::ShardNotFound,
                  str::stream() << "shard " << shardName << " does not exist");
    }

    //
    // Check how many shards belongs to this zone.
    //

    const auto shardDocs = uassertStatusOK(configShard->exhaustiveFindOnConfig(
                                               opCtx,
                                               kConfigPrimarySelector,
                                               repl::ReadConcernLevel::kLocalReadConcern,
                                               shardNS,
                                               BSON(ShardType::tags() << zoneName),
                                               BSONObj(),
                                               2))
                               .docs;


    if (shardDocs.size() == 0) {
        // The zone doesn't exists, this could be a retry.
        return;
    }

    if (shardDocs.size() == 1) {
        auto shardDoc = uassertStatusOK(ShardType::fromBSON(shardDocs.front()));

        if (shardDoc.getName() != shardName) {
            // The last shard that belongs to this zone is a different shard.
            // This could be a retry, so return OK.
            return;
        }

        auto findChunkRange = uassertStatusOK(
            configShard->exhaustiveFindOnConfig(opCtx,
                                                kConfigPrimarySelector,
                                                repl::ReadConcernLevel::kLocalReadConcern,
                                                TagsType::ConfigNS,
                                                BSON(TagsType::tag() << zoneName),
                                                BSONObj(),
                                                1));


        if (findChunkRange.docs.size() > 0) {
            uasserted(ErrorCodes::ZoneStillInUse,
                      "cannot remove a shard from zone if a chunk range is associated with it");
        }
    }

    //
    // Perform update.
    //

    auto updateResult = uassertStatusOK(Grid::get(opCtx)->catalogClient()->updateConfigDocument(
        opCtx,
        ShardType::ConfigNS,
        BSON(ShardType::name(shardName)),
        BSON("$pull" << BSON(ShardType::tags() << zoneName)),
        false,
        kNoWaitWriteConcern));

    // The update did not match a document, another thread could have removed it.
    if (!updateResult) {
        uasserted(ErrorCodes::ShardNotFound,
                  str::stream() << "shard " << shardName << " no longer exist");
    }
}


void ShardingCatalogManager::assignKeyRangeToZone(OperationContext* opCtx,
                                                  const NamespaceString& nss,
                                                  const ChunkRange& givenRange,
                                                  const std::string& zoneName) {
    uassertStatusOK(ShardKeyPattern::checkShardKeyIsValidForMetadataStorage(givenRange.getMin()));
    uassertStatusOK(ShardKeyPattern::checkShardKeyIsValidForMetadataStorage(givenRange.getMax()));

    Lock::ExclusiveLock lk(opCtx->lockState(), _kZoneOpLock);

    auto configServer = Grid::get(opCtx)->shardRegistry()->getConfigShard();

    KeyPattern shardKeyPattern{BSONObj()};

    const auto fullShardKeyRange = [&]() -> ChunkRange {
        try {
            return includeFullShardKey(
                opCtx, configServer.get(), nss, givenRange, &shardKeyPattern);
        } catch (const ExceptionFor<ErrorCodes::NamespaceNotSharded>&) {
            uassertStatusOK(givenRange.extractKeyPattern(&shardKeyPattern));
            return givenRange;
        }
    }();

    const std::size_t zoneCount = uassertStatusOK(configServer->exhaustiveFindOnConfig(
                                                      opCtx,
                                                      kConfigPrimarySelector,
                                                      repl::ReadConcernLevel::kLocalReadConcern,
                                                      ShardType::ConfigNS,
                                                      BSON(ShardType::tags() << zoneName),
                                                      BSONObj(),
                                                      1))
                                      .docs.size();

    const bool zoneExist = zoneCount > 0;
    if (!zoneExist) {
        uasserted(ErrorCodes::ZoneNotFound,
                  (str::stream() << "zone " << zoneName << " does not exist"));
    }

    checkForOveralappedZonedKeyRange(
        opCtx, configServer.get(), nss, fullShardKeyRange, zoneName, shardKeyPattern);

    BSONObj updateQuery(
        BSON("_id" << BSON(TagsType::ns(nss.ns()) << TagsType::min(fullShardKeyRange.getMin()))));

    BSONObjBuilder updateBuilder;
    updateBuilder.append("_id",
                         BSON(TagsType::ns(nss.ns()) << TagsType::min(fullShardKeyRange.getMin())));
    updateBuilder.append(TagsType::ns(), nss.ns());
    updateBuilder.append(TagsType::min(), fullShardKeyRange.getMin());
    updateBuilder.append(TagsType::max(), fullShardKeyRange.getMax());
    updateBuilder.append(TagsType::tag(), zoneName);

    uassertStatusOK(Grid::get(opCtx)->catalogClient()->updateConfigDocument(
        opCtx, TagsType::ConfigNS, updateQuery, updateBuilder.obj(), true, kNoWaitWriteConcern));
}

void ShardingCatalogManager::removeKeyRangeFromZone(OperationContext* opCtx,
                                                    const NamespaceString& nss,
                                                    const ChunkRange& range) {
    Lock::ExclusiveLock lk(opCtx->lockState(), _kZoneOpLock);

    auto configServer = Grid::get(opCtx)->shardRegistry()->getConfigShard();

    KeyPattern shardKeyPattern{BSONObj()};
    try {
        includeFullShardKey(opCtx, configServer.get(), nss, range, &shardKeyPattern);
    } catch (const ExceptionFor<ErrorCodes::NamespaceNotSharded>&) { /* Okay to ignore this */
    }

    BSONObjBuilder removeBuilder;
    removeBuilder.append("_id", BSON(TagsType::ns(nss.ns()) << TagsType::min(range.getMin())));
    removeBuilder.append(TagsType::max(), range.getMax());

    uassertStatusOK(Grid::get(opCtx)->catalogClient()->removeConfigDocuments(
        opCtx, TagsType::ConfigNS, removeBuilder.obj(), kNoWaitWriteConcern));
}

}  // namespace mongo
