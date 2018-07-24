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

#include "mongo/platform/basic.h"

#include "mongo/db/pipeline/document_source_out_drop_target.h"

namespace mongo {

static AtomicUInt32 aggOutCounter;

void DocumentSourceOutDropTarget::initializeWriteNs() {
    DBClientBase* conn = pExpCtx->mongoProcessInterface->directClient();

    const auto& outputNs = getOutputNs();
    _tempNs = NamespaceString(str::stream() << outputNs.db() << ".tmp.agg_out."
                                            << aggOutCounter.addAndFetch(1));

    // Save the original collection options and index specs so we can check they didn't change
    // during computation.
    _originalOutOptions = pExpCtx->mongoProcessInterface->getCollectionOptions(outputNs);
    _originalIndexes = conn->getIndexSpecs(outputNs.ns());

    // Check if it's sharded or capped to make sure we have a chance of succeeding before we do
    // all the work. If the collection becomes capped during processing, the collection options will
    // have changed, and the $out will fail. If it becomes sharded during processing, the final
    // rename will fail.
    uassert(17017,
            str::stream() << "namespace '" << outputNs.ns()
                          << "' is sharded so it can't be used for $out'",
            !pExpCtx->mongoProcessInterface->isSharded(pExpCtx->opCtx, outputNs));
    uassert(17152,
            str::stream() << "namespace '" << outputNs.ns()
                          << "' is capped so it can't be used for $out",
            _originalOutOptions["capped"].eoo());

    // We will write all results into a temporary collection, then rename the temporary
    // collection to be the target collection once we are done.
    _tempNs = NamespaceString(str::stream() << outputNs.db() << ".tmp.agg_out."
                                            << aggOutCounter.addAndFetch(1));

    // Create temp collection, copying options from the existing output collection if any.
    {
        BSONObjBuilder cmd;
        cmd << "create" << _tempNs.coll();
        cmd << "temp" << true;
        cmd.appendElementsUnique(_originalOutOptions);

        BSONObj info;
        uassert(16994,
                str::stream() << "failed to create temporary $out collection '" << _tempNs.ns()
                              << "': "
                              << info.toString(),
                conn->runCommand(outputNs.db().toString(), cmd.done(), info));
    }

    // Copy the indexes of the output collection to the temp collection.
    for (auto indexSpec : _originalIndexes) {
        MutableDocument index((Document(indexSpec)));
        index.remove("_id");  // indexes shouldn't have _ids but some existing ones do
        index["ns"] = Value(_tempNs.ns());

        BSONObj indexBson = index.freeze().toBson();
        conn->insert(_tempNs.getSystemIndexesCollection(), indexBson);
        BSONObj err = conn->getLastErrorDetailed();
        uassert(16995,
                str::stream() << "copying index for $out failed."
                              << " index: "
                              << indexBson
                              << " error: "
                              << err,
                DBClientBase::getLastErrorString(err).empty());
    }
};

void DocumentSourceOutDropTarget::finalize() {
    const auto& outputNs = getOutputNs();
    auto renameCommandObj =
        BSON("renameCollection" << _tempNs.ns() << "to" << outputNs.ns() << "dropTarget" << true);

    pExpCtx->mongoProcessInterface->renameIfOptionsAndIndexesHaveNotChanged(
        pExpCtx->opCtx, renameCommandObj, outputNs, _originalOutOptions, _originalIndexes);
};

}  // namespace mongo
