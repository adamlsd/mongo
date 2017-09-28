/**
 *    Copyright (C) 2016 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include <boost/intrusive_ptr.hpp>
#include <deque>
#include <string>
#include <vector>

#include "mongo/bson/bsonelement.h"
#include "mongo/bson/bsonmisc.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/json.h"
#include "mongo/db/pipeline/aggregation_context_fixture.h"
#include "mongo/db/pipeline/dependencies.h"
#include "mongo/db/pipeline/document_source_mock.h"
#include "mongo/db/pipeline/document_source_sort.h"
#include "mongo/db/pipeline/document_value_test_util.h"
#include "mongo/db/pipeline/pipeline.h"
#include "mongo/unittest/temp_dir.h"
#include "mongo/unittest/unittest.h"

namespace mongo {

namespace {

using boost::intrusive_ptr;
using std::deque;
using std::string;
using std::vector;

static const BSONObj metaTextScore = BSON("$meta"
                                          << "textScore");

class DocumentSourceSortTest : public AggregationContextFixture {
protected:
    void createSort(const BSONObj& sortKey = BSON("a" << 1)) {
        BSONObj spec = BSON("$sort" << sortKey);
        BSONElement specElement = spec.firstElement();
        _sort = DocumentSourceSort::createFromBson(specElement, getExpCtx());
        checkBsonRepresentation(spec);
    }
    DocumentSourceSort* sort() {
        return dynamic_cast<DocumentSourceSort*>(_sort.get());
    }
    /** Assert that iterator state accessors consistently report the source is exhausted. */
    void assertEOF() const {
        ASSERT(_sort->getNext().isEOF());
        ASSERT(_sort->getNext().isEOF());
        ASSERT(_sort->getNext().isEOF());
    }

private:
    /**
     * Check that the BSON representation generated by the souce matches the BSON it was
     * created with.
     */
    void checkBsonRepresentation(const BSONObj& spec) {
        vector<Value> arr;
        _sort->serializeToArray(arr);
        BSONObj generatedSpec = arr[0].getDocument().toBson();
        ASSERT_BSONOBJ_EQ(spec, generatedSpec);
    }
    intrusive_ptr<DocumentSource> _sort;
};


TEST_F(DocumentSourceSortTest, RejectsNonObjectSpec) {
    BSONObj spec = BSON("$sort" << 1);
    BSONElement specElement = spec.firstElement();
    ASSERT_THROWS(DocumentSourceSort::createFromBson(specElement, getExpCtx()), AssertionException);
}

TEST_F(DocumentSourceSortTest, RejectsEmptyObjectSpec) {
    BSONObj spec = BSON("$sort" << BSONObj());
    BSONElement specElement = spec.firstElement();
    ASSERT_THROWS(DocumentSourceSort::createFromBson(specElement, getExpCtx()), AssertionException);
}

TEST_F(DocumentSourceSortTest, RejectsSpecWithNonNumericValues) {
    BSONObj spec = BSON("$sort" << BSON("a"
                                        << "b"));
    BSONElement specElement = spec.firstElement();
    ASSERT_THROWS(DocumentSourceSort::createFromBson(specElement, getExpCtx()), AssertionException);
}

TEST_F(DocumentSourceSortTest, RejectsSpecWithZeroAsValue) {
    BSONObj spec = BSON("$sort" << BSON("a" << 0));
    BSONElement specElement = spec.firstElement();
    ASSERT_THROWS(DocumentSourceSort::createFromBson(specElement, getExpCtx()), AssertionException);
}

TEST_F(DocumentSourceSortTest, SortWithLimit) {
    auto expCtx = getExpCtx();
    createSort(BSON("a" << 1));

    ASSERT_EQUALS(sort()->getLimit(), -1);
    Pipeline::SourceContainer container;
    container.push_back(sort());

    {  // pre-limit checks
        vector<Value> arr;
        sort()->serializeToArray(arr);
        ASSERT_BSONOBJ_EQ(arr[0].getDocument().toBson(), BSON("$sort" << BSON("a" << 1)));

        ASSERT(sort()->getShardSource() != nullptr);
        ASSERT(sort()->getMergeSource() != nullptr);
    }

    container.push_back(DocumentSourceLimit::create(expCtx, 10));
    sort()->optimizeAt(container.begin(), &container);
    ASSERT_EQUALS(container.size(), 1U);
    ASSERT_EQUALS(sort()->getLimit(), 10);

    // unchanged
    container.push_back(DocumentSourceLimit::create(expCtx, 15));
    sort()->optimizeAt(container.begin(), &container);
    ASSERT_EQUALS(container.size(), 1U);
    ASSERT_EQUALS(sort()->getLimit(), 10);

    // reduced
    container.push_back(DocumentSourceLimit::create(expCtx, 5));
    sort()->optimizeAt(container.begin(), &container);
    ASSERT_EQUALS(container.size(), 1U);
    ASSERT_EQUALS(sort()->getLimit(), 5);

    vector<Value> arr;
    sort()->serializeToArray(arr);
    ASSERT_VALUE_EQ(
        Value(arr),
        DOC_ARRAY(DOC("$sort" << DOC("a" << 1)) << DOC("$limit" << sort()->getLimit())));

    ASSERT(sort()->getShardSource() != nullptr);
    ASSERT(sort()->getMergeSource() != nullptr);
}

TEST_F(DocumentSourceSortTest, Dependencies) {
    createSort(BSON("a" << 1 << "b.c" << -1));
    DepsTracker dependencies;
    ASSERT_EQUALS(DocumentSource::SEE_NEXT, sort()->getDependencies(&dependencies));
    ASSERT_EQUALS(2U, dependencies.fields.size());
    ASSERT_EQUALS(1U, dependencies.fields.count("a"));
    ASSERT_EQUALS(1U, dependencies.fields.count("b.c"));
    ASSERT_EQUALS(false, dependencies.needWholeDocument);
    ASSERT_EQUALS(false, dependencies.getNeedTextScore());
}

TEST_F(DocumentSourceSortTest, OutputSort) {
    createSort(BSON("a" << 1 << "b.c" << -1));
    BSONObjSet outputSort = sort()->getOutputSorts();
    ASSERT_EQUALS(outputSort.count(BSON("a" << 1)), 1U);
    ASSERT_EQUALS(outputSort.count(BSON("a" << 1 << "b.c" << -1)), 1U);
    ASSERT_EQUALS(outputSort.size(), 2U);
}

TEST_F(DocumentSourceSortTest, ReportsNoPathsModified) {
    createSort(BSON("a" << 1 << "b.c" << -1));
    auto modifiedPaths = sort()->getModifiedPaths();
    ASSERT(modifiedPaths.type == DocumentSource::GetModPathsReturn::Type::kFiniteSet);
    ASSERT_EQUALS(0U, modifiedPaths.paths.size());
}

class DocumentSourceSortExecutionTest : public DocumentSourceSortTest {
public:
    void checkResults(deque<DocumentSource::GetNextResult> inputDocs,
                      BSONObj sortSpec,
                      string expectedResultSetString) {
        createSort(sortSpec);
        auto source = DocumentSourceMock::create(inputDocs);
        sort()->setSource(source.get());

        // Load the results from the DocumentSourceUnwind.
        vector<Document> resultSet;
        for (auto output = sort()->getNext(); output.isAdvanced(); output = sort()->getNext()) {
            // Get the current result.
            resultSet.push_back(output.releaseDocument());
        }
        // Verify the DocumentSourceUnwind is exhausted.
        assertEOF();

        // Convert results to BSON once they all have been retrieved (to detect any errors
        // resulting from incorrectly shared sub objects).
        BSONArrayBuilder bsonResultSet;
        for (auto&& result : resultSet) {
            bsonResultSet << result;
        }
        // Check the result set.
        ASSERT_BSONOBJ_EQ(expectedResultSet(expectedResultSetString), bsonResultSet.arr());
    }

protected:
    virtual BSONObj expectedResultSet(string expectedResultSetString) {
        BSONObj wrappedResult =
            // fromjson cannot parse an array, so place the array within an object.
            fromjson(string("{'':") + expectedResultSetString + "}");
        return wrappedResult[""].embeddedObject().getOwned();
    }
};

TEST_F(DocumentSourceSortExecutionTest, ShouldGiveNoOutputIfGivenNoInputs) {
    checkResults({}, BSON("a" << 1), "[]");
}

TEST_F(DocumentSourceSortExecutionTest, ShouldGiveOneOutputIfGivenOneInput) {
    checkResults({Document{{"_id", 0}, {"a", 1}}}, BSON("a" << 1), "[{_id:0,a:1}]");
}

TEST_F(DocumentSourceSortExecutionTest, ShouldSortTwoInputsAccordingToOneFieldAscending) {
    checkResults({Document{{"_id", 0}, {"a", 2}}, Document{{"_id", 1}, {"a", 1}}},
                 BSON("a" << 1),
                 "[{_id:1,a:1},{_id:0,a:2}]");
}

/** Sort spec with a descending field. */
TEST_F(DocumentSourceSortExecutionTest, DescendingOrder) {
    checkResults({Document{{"_id", 0}, {"a", 2}}, Document{{"_id", 1}, {"a", 1}}},
                 BSON("a" << -1),
                 "[{_id:0,a:2},{_id:1,a:1}]");
}

/** Sort spec with a dotted field. */
TEST_F(DocumentSourceSortExecutionTest, DottedSortField) {
    checkResults({Document{{"_id", 0}, {"a", Document{{"b", 2}}}},
                  Document{{"_id", 1}, {"a", Document{{"b", 1}}}}},
                 BSON("a.b" << 1),
                 "[{_id:1,a:{b:1}},{_id:0,a:{b:2}}]");
}

/** Sort spec with a compound key. */
TEST_F(DocumentSourceSortExecutionTest, CompoundSortSpec) {
    checkResults({Document{{"_id", 0}, {"a", 1}, {"b", 3}},
                  Document{{"_id", 1}, {"a", 1}, {"b", 2}},
                  Document{{"_id", 2}, {"a", 0}, {"b", 4}}},
                 BSON("a" << 1 << "b" << 1),
                 "[{_id:2,a:0,b:4},{_id:1,a:1,b:2},{_id:0,a:1,b:3}]");
}

/** Sort spec with a compound key and descending order. */
TEST_F(DocumentSourceSortExecutionTest, CompoundSortSpecAlternateOrder) {
    checkResults({Document{{"_id", 0}, {"a", 1}, {"b", 3}},
                  Document{{"_id", 1}, {"a", 1}, {"b", 2}},
                  Document{{"_id", 2}, {"a", 0}, {"b", 4}}},
                 BSON("a" << -1 << "b" << 1),
                 "[{_id:1,a:1,b:2},{_id:0,a:1,b:3},{_id:2,a:0,b:4}]");
}

/** Sort spec with a compound key and descending order. */
TEST_F(DocumentSourceSortExecutionTest, CompoundSortSpecAlternateOrderSecondField) {
    checkResults({Document{{"_id", 0}, {"a", 1}, {"b", 3}},
                  Document{{"_id", 1}, {"a", 1}, {"b", 2}},
                  Document{{"_id", 2}, {"a", 0}, {"b", 4}}},
                 BSON("a" << 1 << "b" << -1),
                 "[{_id:2,a:0,b:4},{_id:0,a:1,b:3},{_id:1,a:1,b:2}]");
}

/** Sorting different types is not supported. */
TEST_F(DocumentSourceSortExecutionTest, InconsistentTypeSort) {
    checkResults({Document{{"_id", 0}, {"a", 1}}, Document{{"_id", 1}, {"a", "foo"_sd}}},
                 BSON("a" << 1),
                 "[{_id:0,a:1},{_id:1,a:\"foo\"}]");
}

/** Sorting different numeric types is supported. */
TEST_F(DocumentSourceSortExecutionTest, MixedNumericSort) {
    checkResults({Document{{"_id", 0}, {"a", 2.3}}, Document{{"_id", 1}, {"a", 1}}},
                 BSON("a" << 1),
                 "[{_id:1,a:1},{_id:0,a:2.3}]");
}

/** Ordering of a missing value. */
TEST_F(DocumentSourceSortExecutionTest, MissingValue) {
    checkResults({Document{{"_id", 0}, {"a", 1}}, Document{{"_id", 1}}},
                 BSON("a" << 1),
                 "[{_id:1},{_id:0,a:1}]");
}

/** Ordering of a null value. */
TEST_F(DocumentSourceSortExecutionTest, NullValue) {
    checkResults({Document{{"_id", 0}, {"a", 1}}, Document{{"_id", 1}, {"a", BSONNULL}}},
                 BSON("a" << 1),
                 "[{_id:1,a:null},{_id:0,a:1}]");
}

/**
 * Order by text score.
 */
TEST_F(DocumentSourceSortExecutionTest, TextScore) {
    MutableDocument first(Document{{"_id", 0}});
    first.setTextScore(10);
    MutableDocument second(Document{{"_id", 1}});
    second.setTextScore(20);

    checkResults({first.freeze(), second.freeze()},
                 BSON("$computed0" << metaTextScore),
                 "[{_id:1},{_id:0}]");
}

/**
 * Order by random value in metadata.
 */
TEST_F(DocumentSourceSortExecutionTest, RandMeta) {
    MutableDocument first(Document{{"_id", 0}});
    first.setRandMetaField(0.01);
    MutableDocument second(Document{{"_id", 1}});
    second.setRandMetaField(0.02);

    checkResults({first.freeze(), second.freeze()},
                 BSON("$computed0" << BSON("$meta"
                                           << "randVal")),
                 "[{_id:1},{_id:0}]");
}

/** A missing nested object within an array returns an empty array. */
TEST_F(DocumentSourceSortExecutionTest, MissingObjectWithinArray) {
    checkResults({Document{{"_id", 0}, {"a", DOC_ARRAY(1)}},
                  Document{{"_id", 1}, {"a", DOC_ARRAY(DOC("b" << 1))}}},
                 BSON("a.b" << 1),
                 "[{_id:0,a:[1]},{_id:1,a:[{b:1}]}]");
}

/** Compare nested values from within an array. */
TEST_F(DocumentSourceSortExecutionTest, ExtractArrayValues) {
    checkResults({Document{{"_id", 0}, {"a", DOC_ARRAY(DOC("b" << 1) << DOC("b" << 2))}},
                  Document{{"_id", 1}, {"a", DOC_ARRAY(DOC("b" << 1) << DOC("b" << 0))}}},
                 BSON("a.b" << 1),
                 "[{_id:1,a:[{b:1},{b:0}]},{_id:0,a:[{b:1},{b:2}]}]");
}

TEST_F(DocumentSourceSortExecutionTest, ShouldPauseWhenAskedTo) {
    auto sort = DocumentSourceSort::create(getExpCtx(), BSON("a" << 1));
    auto mock = DocumentSourceMock::create({DocumentSource::GetNextResult::makePauseExecution(),
                                            Document{{"a", 0}},
                                            DocumentSource::GetNextResult::makePauseExecution()});
    sort->setSource(mock.get());

    // Should propagate the first pause.
    ASSERT_TRUE(sort->getNext().isPaused());

    // Should load the single document, then pause.
    ASSERT_TRUE(sort->getNext().isPaused());

    // Now it should start giving results.
    auto result = sort->getNext();
    ASSERT_TRUE(result.isAdvanced());
    ASSERT_DOCUMENT_EQ(result.releaseDocument(), (Document{{"a", 0}}));
}

TEST_F(DocumentSourceSortExecutionTest, ShouldResumePopulationBetweenPauses) {
    auto sort = DocumentSourceSort::create(getExpCtx(), BSON("a" << 1));
    auto mock = DocumentSourceMock::create({Document{{"a", 1}},
                                            DocumentSource::GetNextResult::makePauseExecution(),
                                            Document{{"a", 0}}});
    sort->setSource(mock.get());

    // Should load the first document, then propagate the pause.
    ASSERT_TRUE(sort->getNext().isPaused());

    // Should finish loading and start yielding results in sorted order.
    auto result = sort->getNext();
    ASSERT_TRUE(result.isAdvanced());
    ASSERT_DOCUMENT_EQ(result.releaseDocument(), (Document{{"a", 0}}));

    result = sort->getNext();
    ASSERT_TRUE(result.isAdvanced());
    ASSERT_DOCUMENT_EQ(result.releaseDocument(), (Document{{"a", 1}}));

    ASSERT_TRUE(sort->getNext().isEOF());
    ASSERT_TRUE(sort->getNext().isEOF());
    ASSERT_TRUE(sort->getNext().isEOF());
}

TEST_F(DocumentSourceSortExecutionTest, ShouldBeAbleToPauseLoadingWhileSpilled) {
    auto expCtx = getExpCtx();

    // Allow the $sort stage to spill to disk.
    unittest::TempDir tempDir("DocumentSourceSortTest");
    expCtx->tempDir = tempDir.path();
    expCtx->allowDiskUse = true;
    const size_t maxMemoryUsageBytes = 1000;

    auto sort = DocumentSourceSort::create(expCtx, BSON("_id" << -1), -1, maxMemoryUsageBytes);

    string largeStr(maxMemoryUsageBytes, 'x');
    auto mock = DocumentSourceMock::create({Document{{"_id", 0}, {"largeStr", largeStr}},
                                            DocumentSource::GetNextResult::makePauseExecution(),
                                            Document{{"_id", 1}, {"largeStr", largeStr}},
                                            DocumentSource::GetNextResult::makePauseExecution(),
                                            Document{{"_id", 2}, {"largeStr", largeStr}}});
    sort->setSource(mock.get());

    // There were 2 pauses, so we should expect 2 paused results before any results can be returned.
    ASSERT_TRUE(sort->getNext().isPaused());
    ASSERT_TRUE(sort->getNext().isPaused());

    // Now we expect to get the results back, sorted by _id descending.
    auto next = sort->getNext();
    ASSERT_TRUE(next.isAdvanced());
    ASSERT_VALUE_EQ(next.releaseDocument()["_id"], Value(2));

    next = sort->getNext();
    ASSERT_TRUE(next.isAdvanced());
    ASSERT_VALUE_EQ(next.releaseDocument()["_id"], Value(1));

    next = sort->getNext();
    ASSERT_TRUE(next.isAdvanced());
    ASSERT_VALUE_EQ(next.releaseDocument()["_id"], Value(0));
}

TEST_F(DocumentSourceSortExecutionTest,
       ShouldErrorIfNotAllowedToSpillToDiskAndResultSetIsTooLarge) {
    auto expCtx = getExpCtx();
    expCtx->allowDiskUse = false;
    const size_t maxMemoryUsageBytes = 1000;

    auto sort = DocumentSourceSort::create(expCtx, BSON("_id" << -1), -1, maxMemoryUsageBytes);

    string largeStr(maxMemoryUsageBytes, 'x');
    auto mock = DocumentSourceMock::create({Document{{"_id", 0}, {"largeStr", largeStr}},
                                            Document{{"_id", 1}, {"largeStr", largeStr}}});
    sort->setSource(mock.get());

    ASSERT_THROWS_CODE(sort->getNext(), AssertionException, 16819);
}

TEST_F(DocumentSourceSortExecutionTest, ShouldCorrectlyTrackMemoryUsageBetweenPauses) {
    auto expCtx = getExpCtx();
    expCtx->allowDiskUse = false;
    const size_t maxMemoryUsageBytes = 1000;

    auto sort = DocumentSourceSort::create(expCtx, BSON("_id" << -1), -1, maxMemoryUsageBytes);

    string largeStr(maxMemoryUsageBytes / 2, 'x');
    auto mock = DocumentSourceMock::create({Document{{"_id", 0}, {"largeStr", largeStr}},
                                            DocumentSource::GetNextResult::makePauseExecution(),
                                            Document{{"_id", 1}, {"largeStr", largeStr}},
                                            Document{{"_id", 2}, {"largeStr", largeStr}}});
    sort->setSource(mock.get());

    // The first getNext() should pause.
    ASSERT_TRUE(sort->getNext().isPaused());

    // The next should realize it's used too much memory.
    ASSERT_THROWS_CODE(sort->getNext(), AssertionException, 16819);
}

}  // namespace
}  // namespace mongo
