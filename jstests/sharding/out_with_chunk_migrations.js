// Tests that the $out aggregation stage is resilient to chunk migrations in both the source and
// output collection during execution.
(function() {
    'use strict';

    const st = new ShardingTest({shards: 2, rs: {nodes: 1}});

    const mongosDB = st.s.getDB(jsTestName());
    const sourceColl = mongosDB["source"];
    const targetColl = mongosDB["target"];

    function setAggHang(mode) {
        assert.commandWorked(st.shard0.adminCommand(
            {configureFailPoint: "hangBeforeDocumentSourceCursorLoadBatch", mode: mode}));
        assert.commandWorked(st.shard1.adminCommand(
            {configureFailPoint: "hangBeforeDocumentSourceCursorLoadBatch", mode: mode}));
    }

    function runOutWithMode(outMode, shardedColl) {
        // Set the failpoint to hang in the first call to DocumentSourceCursor's getNext().
        setAggHang("alwaysOn");

        let comment = outMode + "_" + shardedColl.getName() + "_1";
        // The $_internalInhibitOptimization stage is added to the pipeline to prevent the pipeline
        // from being optimized away after it's been split. Otherwise, we won't hit the failpoint.
        let outFn = `
            const sourceDB = db.getSiblingDB(jsTestName());
            const sourceColl = sourceDB["${sourceColl.getName()}"];
            sourceColl.aggregate([{$_internalInhibitOptimization: {}},
                {$out: {to: "${targetColl.getName()}", mode: "${outMode}"}}],
                {comment: "${comment}"});
        `;

        // Start the $out aggregation in a parallel shell.
        let outShell = startParallelShell(outFn, st.s.port);

        // Wait for the parallel shell to hit the failpoint.
        assert.soon(
            () =>
                mongosDB.currentOp({op: "command", "command.comment": comment}).inprog.length == 1,
            () => tojson(mongosDB.currentOp().inprog));

        // Migrate the chunk on shard1 to shard0.
        assert.commandWorked(st.s.adminCommand(
            {moveChunk: shardedColl.getFullName(), find: {shardKey: 1}, to: st.shard0.shardName}));

        // Unset the failpoint to unblock the $out and join with the parallel shell.
        setAggHang("off");
        outShell();

        // Verify that the $out succeeded.
        assert.eq(2, targetColl.find().itcount());

        // Now both chunks are on shard0. Run a similar test except migrate the chunks back to
        // shard1 in the middle of execution.
        assert.commandWorked(targetColl.remove({}));
        setAggHang("alwaysOn");
        comment = outMode + "_" + shardedColl.getName() + "_2";
        // The $_internalInhibitOptimization stage is added to the pipeline to prevent the pipeline
        // from being optimized away after it's been split. Otherwise, we won't hit the failpoint.
        outFn = `
            const sourceDB = db.getSiblingDB(jsTestName());
            const sourceColl = sourceDB["${sourceColl.getName()}"];
            sourceColl.aggregate([{$_internalInhibitOptimization: {}},
                {$out: {to: "${targetColl.getName()}", mode: "${outMode}"}}],
                {comment: "${comment}"});
        `;
        outShell = startParallelShell(outFn, st.s.port);

        // Wait for the parallel shell to hit the failpoint.
        assert.soon(
            () =>
                mongosDB.currentOp({op: "command", "command.comment": comment}).inprog.length == 1,
            () => tojson(mongosDB.currentOp().inprog));

        assert.commandWorked(st.s.adminCommand(
            {moveChunk: shardedColl.getFullName(), find: {shardKey: -1}, to: st.shard1.shardName}));
        assert.commandWorked(st.s.adminCommand(
            {moveChunk: shardedColl.getFullName(), find: {shardKey: 1}, to: st.shard1.shardName}));

        // Unset the failpoint to unblock the $out and join with the parallel shell.
        setAggHang("off");
        outShell();

        // Verify that the $out succeeded.
        assert.eq(2, targetColl.find().itcount());

        // Reset the chunk distribution.
        assert.commandWorked(st.s.adminCommand(
            {moveChunk: shardedColl.getFullName(), find: {shardKey: -1}, to: st.shard0.shardName}));

        assert.commandWorked(targetColl.remove({}));
    }

    // Shard the source collection with shard key {shardKey: 1} and split into 2 chunks.
    st.shardColl(sourceColl.getName(), {shardKey: 1}, {shardKey: 0}, false, mongosDB.getName());

    // Write a document to each chunk of the source collection.
    assert.commandWorked(sourceColl.insert({shardKey: -1}));
    assert.commandWorked(sourceColl.insert({shardKey: 1}));

    runOutWithMode("replaceCollection", sourceColl);
    runOutWithMode("replaceDocuments", sourceColl);
    runOutWithMode("insertDocuments", sourceColl);

    // Run a similar test with chunk migrations on the output collection instead.
    sourceColl.drop();

    // Shard the output collection with shard key {shardKey: 1} and split into 2 chunks.
    st.shardColl(targetColl.getName(), {shardKey: 1}, {shardKey: 0}, false, mongosDB.getName());

    // Write two documents in the source collection that should target the two chunks in the target
    // collection.
    assert.commandWorked(sourceColl.insert({shardKey: -1}));
    assert.commandWorked(sourceColl.insert({shardKey: 1}));

    // Note that mode "replaceCollection" is not supported with an existing sharded output
    // collection.
    runOutWithMode("replaceDocuments", targetColl);
    runOutWithMode("insertDocuments", targetColl);

    st.stop();
})();
