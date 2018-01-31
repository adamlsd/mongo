/**
 * Copyright (C) 2018 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/platform/basic.h"

#include "mongo/db/catalog/catalog_control.h"

#include "mongo/db/catalog/collection.h"
#include "mongo/db/catalog/database.h"
#include "mongo/db/catalog/database_catalog_entry.h"
#include "mongo/db/catalog/database_holder.h"
#include "mongo/db/catalog/uuid_catalog.h"
#include "mongo/db/ftdc/ftdc_mongod.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/repair_database.h"
#include "mongo/util/log.h"

namespace mongo {
namespace catalog {
void closeCatalog(OperationContext* opCtx) {
    invariant(opCtx->lockState()->isW());

    // Close all databases.
    log() << "closeCatalog: closing all databases in dbholder";
    BSONObjBuilder closeDbsBuilder;
    constexpr auto force = true;
    constexpr auto reason = "closing databases for closeCatalog";
    uassert(40687,
            str::stream() << "failed to close all databases; result of operation: "
                          << closeDbsBuilder.obj().jsonString(),
            dbHolder().closeAll(opCtx, closeDbsBuilder, force, reason));

    // Because we've force-closed the database, there should be no databases left open.
    auto closeDbsResult = closeDbsBuilder.obj();
    invariant(
        !closeDbsResult.hasField("nNotClosed"),
        str::stream() << "expected no databases open after a force close; result of operation: "
                      << closeDbsResult.jsonString());

    // Close the storage engine's catalog.
    log() << "closeCatalog: closing storage engine catalog";
    opCtx->getServiceContext()->getGlobalStorageEngine()->closeCatalog(opCtx);
}

void openCatalog(OperationContext* opCtx) {
    invariant(opCtx->lockState()->isW());

    // Load the catalog in the storage engine.
    log() << "openCatalog: loading storage engine catalog";
    auto storageEngine = opCtx->getServiceContext()->getGlobalStorageEngine();
    storageEngine->loadCatalog(opCtx);

    log() << "openCatalog: reconciling catalog and idents";
    auto indexesToRebuild = storageEngine->reconcileCatalogAndIdents(opCtx);
    fassertStatusOK(40688, indexesToRebuild.getStatus());

    // Rebuild indexes if necessary.
    for (auto indexNamespace : indexesToRebuild.getValue()) {
        NamespaceString collNss(indexNamespace.first);
        auto indexName = indexNamespace.second;

        auto dbCatalogEntry = storageEngine->getDatabaseCatalogEntry(opCtx, collNss.db());
        invariant(dbCatalogEntry,
                  str::stream() << "couldn't get database catalog entry for database "
                                << collNss.db());
        auto collCatalogEntry = dbCatalogEntry->getCollectionCatalogEntry(collNss.toString());
        invariant(collCatalogEntry,
                  str::stream() << "couldn't get collection catalog entry for collection "
                                << collNss.toString());

        auto indexSpecs = getIndexNameObjs(
            opCtx, dbCatalogEntry, collCatalogEntry, [&indexName](const std::string& name) {
                return name == indexName;
            });
        if (!indexSpecs.isOK() || indexSpecs.getValue().first.empty()) {
            fassertStatusOK(40689,
                            {ErrorCodes::InternalError,
                             str::stream() << "failed to get index spec for index " << indexName
                                           << " in collection "
                                           << collNss.toString()});
        }
        auto indexesToRebuild = indexSpecs.getValue();
        invariant(
            indexesToRebuild.first.size() == 1,
            str::stream() << "expected to find a list containing exactly 1 index name, but found "
                          << indexesToRebuild.first.size());
        invariant(
            indexesToRebuild.second.size() == 1,
            str::stream() << "expected to find a list containing exactly 1 index spec, but found "
                          << indexesToRebuild.second.size());

        log() << "openCatalog: rebuilding index " << indexName << " in collection "
              << collNss.toString();
        fassertStatusOK(40690,
                        rebuildIndexesOnCollection(
                            opCtx, dbCatalogEntry, collCatalogEntry, std::move(indexesToRebuild)));
    }

    // Open all databases and repopulate the UUID catalog.
    log() << "openCatalog: reopening all databases";
    auto& uuidCatalog = UUIDCatalog::get(opCtx);
    std::vector<std::string> databasesToOpen;
    storageEngine->listDatabases(&databasesToOpen);
    for (auto&& dbName : databasesToOpen) {
        LOG(1) << "openCatalog: dbholder reopening database " << dbName;
        auto db = dbHolder().openDb(opCtx, dbName);
        invariant(db, str::stream() << "failed to reopen database " << dbName);

        std::list<std::string> collections;
        db->getDatabaseCatalogEntry()->getCollectionNamespaces(&collections);
        for (auto&& collName : collections) {
            // Note that the collection name already includes the database component.
            NamespaceString collNss(collName);
            auto collection = db->getCollection(opCtx, collName);
            invariant(collection,
                      str::stream() << "failed to get valid collection pointer for namespace "
                                    << collName);

            auto uuid = collection->uuid();
            // TODO (SERVER-32597): When the minimum featureCompatibilityVersion becomes 3.6, we
            // can change this condition to be an invariant.
            if (uuid) {
                LOG(1) << "openCatalog: registering uuid " << uuid->toString() << " for collection "
                       << collName;
                uuidCatalog.registerUUIDCatalogEntry(*uuid, collection);
            }

            // If this is the oplog collection, re-establish the replication system's cached pointer
            // to the oplog.
            if (collNss.isOplog()) {
                log() << "openCatalog: updating cached oplog pointer";
                repl::establishOplogCollectionForLogging(opCtx, collection);
            }
        }
    }
}
}  // namespace catalog
}  // namespace mongo
