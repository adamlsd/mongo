// Cannot implicitly shard accessed collections because of following errmsg: Cannot output to a
// non-sharded collection because sharded collection exists already.
// @tags: [
//   assumes_unsharded_collection,
//   # mapReduce does not support afterClusterTime.
//   does_not_support_causal_consistency,
//   does_not_support_stepdowns,
//   uses_map_reduce_with_temp_collections,
// ]

normal = "mr_outreduce2";
out = normal + "_out";

t = db[normal];
t.drop();

db[out].drop();

t.insert({_id: 1, x: 1});
t.insert({_id: 2, x: 1});
t.insert({_id: 3, x: 2});

m = function() {
    emit(this.x, 1);
};
r = function(k, v) {
    return Array.sum(v);
};

res = t.mapReduce(m, r, {out: {reduce: out}, query: {_id: {$gt: 0}}});

assert.eq(2, db[out].findOne({_id: 1}).value, "A1");
assert.eq(1, db[out].findOne({_id: 2}).value, "A2");

t.insert({_id: 4, x: 2});
res = t.mapReduce(m, r, {out: {reduce: out}, query: {_id: {$gt: 3}}, finalize: null});

assert.eq(2, db[out].findOne({_id: 1}).value, "B1");
assert.eq(2, db[out].findOne({_id: 2}).value, "B2");
