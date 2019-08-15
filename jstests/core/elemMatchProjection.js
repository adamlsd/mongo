// @tags: [requires_getmore]

// Tests for $elemMatch projections and $ positional operator projection.
(function() {
"use strict";

const coll = db.SERVER828Test;
coll.drop();

const date1 = new Date();

// Generate monotonically increasing _id values. ObjectIds generated by the shell are not
// guaranteed to be monotically increasing, and we will depend on the _id sort order later in
// the test.
let currentId = 0;
function nextId() {
    return ++currentId;
}

// Insert various styles of arrays.
const bulk = coll.initializeUnorderedBulkOp();
for (let i = 0; i < 100; i++) {
    bulk.insert({_id: nextId(), group: 1, x: [1, 2, 3, 4, 5]});
    bulk.insert({_id: nextId(), group: 2, x: [{a: 1, b: 2}, {a: 2, c: 3}, {a: 1, d: 5}]});
    bulk.insert({
        _id: nextId(),
        group: 3,
        x: [{a: 1, b: 2}, {a: 2, c: 3}, {a: 1, d: 5}],
        y: [{aa: 1, bb: 2}, {aa: 2, cc: 3}, {aa: 1, dd: 5}]
    });
    bulk.insert({_id: nextId(), group: 3, x: [{a: 1, b: 3}, {a: -6, c: 3}]});
    bulk.insert({_id: nextId(), group: 4, x: [{a: 1, b: 4}, {a: -6, c: 3}]});
    bulk.insert(
        {_id: nextId(), group: 5, x: [new Date(), 5, 10, 'string', new ObjectId(), 123.456]});
    bulk.insert({
        _id: nextId(),
        group: 6,
        x: [{a: 'string', b: date1}, {a: new ObjectId(), b: 1.2345}, {a: 'string2', b: date1}]
    });
    bulk.insert({_id: nextId(), group: 7, x: [{y: [1, 2, 3, 4]}]});
    bulk.insert({_id: nextId(), group: 8, x: [{y: [{a: 1, b: 2}, {a: 3, b: 4}]}]});
    bulk.insert({
        _id: nextId(),
        group: 9,
        x: [{y: [{a: 1, b: 2}, {a: 3, b: 4}]}, {z: [{a: 1, b: 2}, {a: 3, b: 4}]}]
    });
    bulk.insert({
        _id: nextId(),
        group: 10,
        x: [{a: 1, b: 2}, {a: 3, b: 4}],
        y: [{c: 1, d: 2}, {c: 3, d: 4}]
    });
    bulk.insert({
        _id: nextId(),
        group: 10,
        x: [{a: 1, b: 2}, {a: 3, b: 4}],
        y: [{c: 1, d: 2}, {c: 3, d: 4}]
    });
    bulk.insert({
        _id: nextId(),
        group: 11,
        x: [{a: 1, b: 2}, {a: 2, c: 3}, {a: 1, d: 5}],
        covered: [{aa: 1, bb: 2}, {aa: 2, cc: 3}, {aa: 1, dd: 5}]
    });
    bulk.insert({_id: nextId(), group: 12, x: {y: [{a: 1, b: 1}, {a: 1, b: 2}]}});
    bulk.insert({_id: nextId(), group: 13, x: [{a: 1, b: 1}, {a: 1, b: 2}]});
    bulk.insert({_id: nextId(), group: 13, x: [{a: 1, b: 2}, {a: 1, b: 1}]});
}
assert.commandWorked(bulk.execute());

assert.commandWorked(coll.createIndex({group: 1, 'y.d': 1}));
assert.commandWorked(coll.createIndex({group: 1, covered: 1}));  // for covered index test

// Tests for the $-positional operator.
assert.eq(1,
          coll.find({group: 3, 'x.a': 2}, {'x.$': 1}).sort({_id: 1}).toArray()[0].x.length,
          "single object match (array length match)");

assert.eq(2,
          coll.find({group: 3, 'x.a': 1}, {'x.$': 1}).sort({_id: 1}).toArray()[0].x[0].b,
          "single object match first");

assert.eq(undefined,
          coll.find({group: 3, 'x.a': 2}, {_id: 0, 'x.$': 1}).sort({_id: 1}).toArray()[0]._id,
          "single object match with filtered _id");

assert.eq(1,
          coll.find({group: 3, 'x.a': 2}, {'x.$': 1}).sort({_id: 1}).toArray()[0].x.length,
          "sorted single object match with filtered _id (array length match)");

assert.eq(1,
          coll.find({'group': 2, 'x': {'$elemMatch': {'a': 1, 'b': 2}}}, {'x.$': 1})
              .toArray()[0]
              .x.length,
          "single object match with elemMatch");

assert.eq(1,
          coll.find({'group': 2, 'x': {'$elemMatch': {'a': 1, 'b': 2}}}, {'x.$': {'$slice': 1}})
              .toArray()[0]
              .x.length,
          "single object match with elemMatch and positive slice");

assert.eq(1,
          coll.find({'group': 2, 'x': {'$elemMatch': {'a': 1, 'b': 2}}}, {'x.$': {'$slice': -1}})
              .toArray()[0]
              .x.length,
          "single object match with elemMatch and negative slice");

assert.eq(1,
          coll.find({'group': 12, 'x.y.a': 1}, {'x.y.$': 1}).toArray()[0].x.y.length,
          "single object match with two level dot notation");

assert.eq(1,
          coll.find({group: 3, 'x.a': 2}, {'x.$': 1}).sort({x: 1}).toArray()[0].x.length,
          "sorted object match (array length match)");

assert.eq({aa: 1, dd: 5},
          coll.find({group: 3, 'y.dd': 5}, {'y.$': 1}).sort({_id: 1}).toArray()[0].y[0],
          "single object match (value match)");

assert.throws(function() {
    coll.find({group: 3, 'x.a': 2}, {'y.$': 1}).toArray();
}, [], "throw on invalid projection (field mismatch)");

assert.throws(function() {
    coll.find({group: 3, 'x.a': 2}, {'y.$': 1}).sort({x: 1}).toArray();
}, [], "throw on invalid sorted projection (field mismatch)");

assert.throws(function() {
    coll.find({group: 3, 'x.a': 2}, {'x.$': 1, group: 0}).sort({x: 1}).toArray();
}, [], "throw on invalid projection combination (include and exclude)");

assert.throws(function() {
    coll.find({group: 3, 'x.a': 1, 'y.aa': 1}, {'x.$': 1, 'y.$': 1}).toArray();
}, [], "throw on multiple projections");

assert.throws(function() {
    coll.find({group: 3}, {'g.$': 1}).toArray();
}, [], "throw on invalid projection (non-array field)");

assert.eq({aa: 1, dd: 5},
          coll.find({group: 11, 'covered.dd': 5}, {'covered.$': 1}).toArray()[0].covered[0],
          "single object match (covered index)");

assert.eq({aa: 1, dd: 5},
          coll.find({group: 11, 'covered.dd': 5}, {'covered.$': 1})
              .sort({covered: 1})
              .toArray()[0]
              .covered[0],
          "single object match (sorted covered index)");

assert.eq(1,
          coll.find({group: 10, 'y.d': 4}, {'y.$': 1}).sort({_id: 1}).toArray()[0].y.length,
          "single object match (regular index");

// Tests for $elemMatch projection.
assert.eq(-6,
          coll.find({group: 4}, {x: {$elemMatch: {a: -6}}}).toArray()[0].x[0].a,
          "single object match");

assert.eq(1,
          coll.find({group: 4}, {x: {$elemMatch: {a: -6}}}).toArray()[0].x.length,
          "filters non-matching array elements");

assert.eq(1,
          coll.find({group: 4}, {x: {$elemMatch: {a: -6, c: 3}}}).toArray()[0].x.length,
          "filters non-matching array elements with multiple elemMatch criteria");

assert.eq(
    1,
    coll.find({group: 13}, {'x': {'$elemMatch': {a: {$gt: 0, $lt: 2}}}})
        .sort({_id: 1})
        .toArray()[0]
        .x.length,
    "filters non-matching array elements with multiple criteria for a single element in the array");

assert.eq(
    3,
    coll.find({group: 4}, {x: {$elemMatch: {a: {$lt: 1}}}}).sort({_id: 1}).toArray()[0].x[0].c,
    "object operator match");

assert.eq([4],
          coll.find({group: 1}, {x: {$elemMatch: {$in: [100, 4, -123]}}}).toArray()[0].x,
          "$in number match");

assert.eq([{a: 1, b: 2}],
          coll.find({group: 2}, {x: {$elemMatch: {a: {$in: [1]}}}}).toArray()[0].x,
          "$in number match");

assert.eq([1],
          coll.find({group: 1}, {x: {$elemMatch: {$nin: [4, 5, 6]}}}).toArray()[0].x,
          "$nin number match");

assert.eq(
    [1], coll.find({group: 1}, {x: {$elemMatch: {$all: [1]}}}).toArray()[0].x, "$in number match");

assert.eq([{a: 'string', b: date1}],
          coll.find({group: 6}, {x: {$elemMatch: {a: 'string'}}}).toArray()[0].x,
          "mixed object match on string eq");

assert.eq([{a: 'string2', b: date1}],
          coll.find({group: 6}, {x: {$elemMatch: {a: /ring2/}}}).toArray()[0].x,
          "mixed object match on regexp");

assert.eq([{a: 'string', b: date1}],
          coll.find({group: 6}, {x: {$elemMatch: {a: {$type: 2}}}}).toArray()[0].x,
          "mixed object match on type");

assert.eq([{a: 2, c: 3}],
          coll.find({group: 2}, {x: {$elemMatch: {a: {$ne: 1}}}}).toArray()[0].x,
          "mixed object match on ne");

assert.eq(
    [{a: 1, d: 5}],
    coll.find({group: 3}, {x: {$elemMatch: {d: {$exists: true}}}}).sort({_id: 1}).toArray()[0].x,
    "mixed object match on exists");

assert.eq(
    [{a: 2, c: 3}],
    coll.find({group: 3}, {x: {$elemMatch: {a: {$mod: [2, 0]}}}}).sort({_id: 1}).toArray()[0].x,
    "mixed object match on mod");

assert.eq({"x": [{"a": 1, "b": 2}], "y": [{"c": 3, "d": 4}]},
          coll.find({group: 10}, {_id: 0, x: {$elemMatch: {a: 1}}, y: {$elemMatch: {c: 3}}})
              .sort({_id: 1})
              .toArray()[0],
          "multiple $elemMatch on unique fields 1");

// Tests involving getMore. Test the $-positional operator across multiple batches.
let a = coll.find({group: 3, 'x.b': 2}, {'x.$': 1}).sort({_id: 1}).batchSize(1);
while (a.hasNext()) {
    assert.eq(2, a.next().x[0].b, "positional getMore test");
}

// Test the $elemMatch operator across multiple batches.
a = coll.find({group: 3}, {x: {$elemMatch: {a: 1}}}).sort({_id: 1}).batchSize(1);
while (a.hasNext()) {
    assert.eq(1, a.next().x[0].a, "positional getMore test");
}
}());
