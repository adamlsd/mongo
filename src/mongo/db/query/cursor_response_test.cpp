/**
 *    Copyright 2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/query/cursor_response.h"

#include "mongo/rpc/op_msg_rpc_impls.h"

#include "mongo/unittest/unittest.h"

namespace mongo {

namespace {

TEST(CursorResponseTest, parseFromBSONFirstBatch) {
    StatusWith<CursorResponse> result = CursorResponse::parseFromBSON(
        BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                   << "db.coll"
                                   << "firstBatch"
                                   << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))
                      << "ok"
                      << 1));
    ASSERT_OK(result.getStatus());

    CursorResponse response = std::move(result.getValue());
    ASSERT_EQ(response.getCursorId(), CursorId(123));
    ASSERT_EQ(response.getNSS().ns(), "db.coll");
    ASSERT_EQ(response.getBatch().size(), 2U);
    ASSERT_BSONOBJ_EQ(response.getBatch()[0], BSON("_id" << 1));
    ASSERT_BSONOBJ_EQ(response.getBatch()[1], BSON("_id" << 2));
    ASSERT_FALSE(response.getLastOplogTimestamp());
}

TEST(CursorResponseTest, parseFromBSONNextBatch) {
    StatusWith<CursorResponse> result = CursorResponse::parseFromBSON(
        BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                   << "db.coll"
                                   << "nextBatch"
                                   << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))
                      << "ok"
                      << 1));
    ASSERT_OK(result.getStatus());

    CursorResponse response = std::move(result.getValue());
    ASSERT_EQ(response.getCursorId(), CursorId(123));
    ASSERT_EQ(response.getNSS().ns(), "db.coll");
    ASSERT_EQ(response.getBatch().size(), 2U);
    ASSERT_BSONOBJ_EQ(response.getBatch()[0], BSON("_id" << 1));
    ASSERT_BSONOBJ_EQ(response.getBatch()[1], BSON("_id" << 2));
    ASSERT_FALSE(response.getLastOplogTimestamp());
}

TEST(CursorResponseTest, parseFromBSONCursorIdZero) {
    StatusWith<CursorResponse> result = CursorResponse::parseFromBSON(
        BSON("cursor" << BSON("id" << CursorId(0) << "ns"
                                   << "db.coll"
                                   << "nextBatch"
                                   << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))
                      << "ok"
                      << 1));
    ASSERT_OK(result.getStatus());

    CursorResponse response = std::move(result.getValue());
    ASSERT_EQ(response.getCursorId(), CursorId(0));
    ASSERT_EQ(response.getNSS().ns(), "db.coll");
    ASSERT_EQ(response.getBatch().size(), 2U);
    ASSERT_BSONOBJ_EQ(response.getBatch()[0], BSON("_id" << 1));
    ASSERT_BSONOBJ_EQ(response.getBatch()[1], BSON("_id" << 2));
}

TEST(CursorResponseTest, parseFromBSONEmptyBatch) {
    StatusWith<CursorResponse> result =
        CursorResponse::parseFromBSON(BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                                                 << "db.coll"
                                                                 << "nextBatch"
                                                                 << BSONArrayBuilder().arr())
                                                    << "ok"
                                                    << 1));
    ASSERT_OK(result.getStatus());

    CursorResponse response = std::move(result.getValue());
    ASSERT_EQ(response.getCursorId(), CursorId(123));
    ASSERT_EQ(response.getNSS().ns(), "db.coll");
    ASSERT_EQ(response.getBatch().size(), 0U);
}

TEST(CursorResponseTest, parseFromBSONLatestOplogEntry) {
    StatusWith<CursorResponse> result =
        CursorResponse::parseFromBSON(BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                                                 << "db.coll"
                                                                 << "nextBatch"
                                                                 << BSONArrayBuilder().arr())
                                                    << "$_internalLatestOplogTimestamp"
                                                    << Timestamp(1, 2)
                                                    << "ok"
                                                    << 1));
    ASSERT_OK(result.getStatus());

    CursorResponse response = std::move(result.getValue());
    ASSERT_EQ(response.getCursorId(), CursorId(123));
    ASSERT_EQ(response.getNSS().ns(), "db.coll");
    ASSERT_EQ(response.getBatch().size(), 0U);
    ASSERT_EQ(*response.getLastOplogTimestamp(), Timestamp(1, 2));
}

TEST(CursorResponseTest, parseFromBSONMissingCursorField) {
    StatusWith<CursorResponse> result = CursorResponse::parseFromBSON(BSON("ok" << 1));
    ASSERT_NOT_OK(result.getStatus());
}

TEST(CursorResponseTest, parseFromBSONCursorFieldWrongType) {
    StatusWith<CursorResponse> result =
        CursorResponse::parseFromBSON(BSON("cursor" << 3 << "ok" << 1));
    ASSERT_NOT_OK(result.getStatus());
}

TEST(CursorResponseTest, parseFromBSONNsFieldMissing) {
    StatusWith<CursorResponse> result = CursorResponse::parseFromBSON(
        BSON("cursor" << BSON("id" << CursorId(123) << "firstBatch"
                                   << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))
                      << "ok"
                      << 1));
    ASSERT_NOT_OK(result.getStatus());
}

TEST(CursorResponseTest, parseFromBSONNsFieldWrongType) {
    StatusWith<CursorResponse> result = CursorResponse::parseFromBSON(
        BSON("cursor" << BSON("id" << CursorId(123) << "ns" << 456 << "firstBatch"
                                   << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))
                      << "ok"
                      << 1));
    ASSERT_NOT_OK(result.getStatus());
}

TEST(CursorResponseTest, parseFromBSONIdFieldMissing) {
    StatusWith<CursorResponse> result = CursorResponse::parseFromBSON(
        BSON("cursor" << BSON("ns"
                              << "db.coll"
                              << "nextBatch"
                              << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))
                      << "ok"
                      << 1));
    ASSERT_NOT_OK(result.getStatus());
}

TEST(CursorResponseTest, parseFromBSONIdFieldWrongType) {
    StatusWith<CursorResponse> result = CursorResponse::parseFromBSON(
        BSON("cursor" << BSON("id"
                              << "123"
                              << "ns"
                              << "db.coll"
                              << "nextBatch"
                              << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))
                      << "ok"
                      << 1));
    ASSERT_NOT_OK(result.getStatus());
}

TEST(CursorResponseTest, parseFromBSONBatchFieldMissing) {
    StatusWith<CursorResponse> result =
        CursorResponse::parseFromBSON(BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                                                 << "db.coll")
                                                    << "ok"
                                                    << 1));
    ASSERT_NOT_OK(result.getStatus());
}

TEST(CursorResponseTest, parseFromBSONFirstBatchFieldWrongType) {
    StatusWith<CursorResponse> result =
        CursorResponse::parseFromBSON(BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                                                 << "db.coll"
                                                                 << "firstBatch"
                                                                 << BSON("_id" << 1))
                                                    << "ok"
                                                    << 1));
    ASSERT_NOT_OK(result.getStatus());
}

TEST(CursorResponseTest, parseFromBSONNextBatchFieldWrongType) {
    StatusWith<CursorResponse> result =
        CursorResponse::parseFromBSON(BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                                                 << "db.coll"
                                                                 << "nextBatch"
                                                                 << BSON("_id" << 1))
                                                    << "ok"
                                                    << 1));
    ASSERT_NOT_OK(result.getStatus());
}

TEST(CursorResponseTest, parseFromBSONLatestOplogEntryWrongType) {
    StatusWith<CursorResponse> result =
        CursorResponse::parseFromBSON(BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                                                 << "db.coll"
                                                                 << "nextBatch"
                                                                 << BSON_ARRAY(BSON("_id" << 1)))
                                                    << "$_internalLatestOplogTimestamp"
                                                    << 1
                                                    << "ok"
                                                    << 1));
    ASSERT_NOT_OK(result.getStatus());
}

TEST(CursorResponseTest, parseFromBSONOkFieldMissing) {
    StatusWith<CursorResponse> result = CursorResponse::parseFromBSON(
        BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                   << "db.coll"
                                   << "nextBatch"
                                   << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))));
    ASSERT_NOT_OK(result.getStatus());
}

TEST(CursorResponseTest, parseFromBSONHandleErrorResponse) {
    StatusWith<CursorResponse> result =
        CursorResponse::parseFromBSON(BSON("ok" << 0 << "code" << 123 << "errmsg"
                                                << "does not work"));
    ASSERT_NOT_OK(result.getStatus());
    ASSERT_EQ(result.getStatus().code(), 123);
    ASSERT_EQ(result.getStatus().reason(), "does not work");
}

TEST(CursorResponseTest, toBSONInitialResponse) {
    std::vector<BSONObj> batch = {BSON("_id" << 1), BSON("_id" << 2)};
    CursorResponse response(NamespaceString("testdb.testcoll"), CursorId(123), batch);
    BSONObj responseObj = response.toBSON(CursorResponse::ResponseType::InitialResponse);
    BSONObj expectedResponse =
        BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                   << "testdb.testcoll"
                                   << "firstBatch"
                                   << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))
                      << "ok"
                      << 1.0);
    ASSERT_BSONOBJ_EQ(responseObj, expectedResponse);
}

TEST(CursorResponseTest, toBSONSubsequentResponse) {
    std::vector<BSONObj> batch = {BSON("_id" << 1), BSON("_id" << 2)};
    CursorResponse response(NamespaceString("testdb.testcoll"), CursorId(123), batch);
    BSONObj responseObj = response.toBSON(CursorResponse::ResponseType::SubsequentResponse);
    BSONObj expectedResponse =
        BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                   << "testdb.testcoll"
                                   << "nextBatch"
                                   << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))
                      << "ok"
                      << 1.0);
    ASSERT_BSONOBJ_EQ(responseObj, expectedResponse);
}

TEST(CursorResponseTest, addToBSONInitialResponse) {
    std::vector<BSONObj> batch = {BSON("_id" << 1), BSON("_id" << 2)};
    CursorResponse response(NamespaceString("testdb.testcoll"), CursorId(123), batch);

    BSONObjBuilder builder;
    response.addToBSON(CursorResponse::ResponseType::InitialResponse, &builder);
    BSONObj responseObj = builder.obj();

    BSONObj expectedResponse =
        BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                   << "testdb.testcoll"
                                   << "firstBatch"
                                   << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))
                      << "ok"
                      << 1.0);
    ASSERT_BSONOBJ_EQ(responseObj, expectedResponse);
}

TEST(CursorResponseTest, addToBSONSubsequentResponse) {
    std::vector<BSONObj> batch = {BSON("_id" << 1), BSON("_id" << 2)};
    CursorResponse response(NamespaceString("testdb.testcoll"), CursorId(123), batch);

    BSONObjBuilder builder;
    response.addToBSON(CursorResponse::ResponseType::SubsequentResponse, &builder);
    BSONObj responseObj = builder.obj();

    BSONObj expectedResponse =
        BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                   << "testdb.testcoll"
                                   << "nextBatch"
                                   << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))
                      << "ok"
                      << 1.0);
    ASSERT_BSONOBJ_EQ(responseObj, expectedResponse);
}

TEST(CursorResponseTest, serializeLatestOplogEntry) {
    std::vector<BSONObj> batch = {BSON("_id" << 1), BSON("_id" << 2)};
    CursorResponse response(
        NamespaceString("db.coll"), CursorId(123), batch, boost::none, Timestamp(1, 2));
    auto serialized = response.toBSON(CursorResponse::ResponseType::SubsequentResponse);
    ASSERT_BSONOBJ_EQ(serialized,
                      BSON("cursor"
                           << BSON("id" << CursorId(123) << "ns"
                                        << "db.coll"
                                        << "nextBatch"
                                        << BSON_ARRAY(BSON("_id" << 1) << BSON("_id" << 2)))
                           << "$_internalLatestOplogTimestamp"
                           << Timestamp(1, 2)
                           << "ok"
                           << 1));
    auto reparsed = CursorResponse::parseFromBSON(serialized);
    ASSERT_OK(reparsed.getStatus());
    CursorResponse reparsedResponse = std::move(reparsed.getValue());
    ASSERT_EQ(reparsedResponse.getCursorId(), CursorId(123));
    ASSERT_EQ(reparsedResponse.getNSS().ns(), "db.coll");
    ASSERT_EQ(reparsedResponse.getBatch().size(), 2U);
    ASSERT_EQ(*reparsedResponse.getLastOplogTimestamp(), Timestamp(1, 2));
}

TEST(CursorResponseTest, cursorReturnDocumentSequences) {
    CursorResponseBuilder::Options options;
    options.isInitialResponse = true;
    options.useDocumentSequences = true;
    rpc::OpMsgReplyBuilder builder;
    BSONObj expectedDoc = BSON("_id" << 1 << "test"
                                     << "123");
    BSONObj expectedBody = BSON("cursor" << BSON("id" << CursorId(123) << "ns"
                                                      << "db.coll"));

    CursorResponseBuilder crb(&builder, options);
    crb.append(expectedDoc);
    ASSERT_EQ(crb.numDocs(), 1U);
    crb.done(CursorId(123), "db.coll");

    auto msg = builder.done();
    auto opMsg = OpMsg::parse(msg);
    const auto& docSeqs = opMsg.sequences;
    ASSERT_EQ(docSeqs.size(), 1U);
    const auto& documentSequence = docSeqs[0];
    ASSERT_EQ(documentSequence.name, "cursor.firstBatch");
    ASSERT_EQ(documentSequence.objs.size(), 1U);
    ASSERT_BSONOBJ_EQ(documentSequence.objs[0], expectedDoc);
    ASSERT_BSONOBJ_EQ(opMsg.body, expectedBody);
}

}  // namespace

}  // namespace mongo
