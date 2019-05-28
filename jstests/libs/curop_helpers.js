// Wait until the current operation matches the filter.
function waitForCurOpByFilter(db, filter) {
    const adminDB = db.getSiblingDB("admin");
    assert.soon(
        () => {
            return adminDB.aggregate([{$currentOp: {}}, {$match: filter}]).itcount() == 1;
        },
        () => {
            return "Failed to find a matching op for filter \"" + tojson(filter) +
                "\" in currentOp output: " +
                tojson(adminDB.aggregate([{$currentOp: {}}]).toArray());
        });
}

// Wait until the current operation reaches the fail point "failPoint" for the given
// namespace "nss".
function waitForCurOpByFailPoint(db, nss, failPoint) {
    let filter = {$and: [{"ns": nss}, {"msg": failPoint}]};
    waitForCurOpByFilter(db, filter);
}
