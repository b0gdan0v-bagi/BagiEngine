// Runs that took nothing: sets `worker.noClaim` (media/runs.js) on every worker of a read, and keeps what worker.py
// forgets when a session ends, the entry a loop was started on and did not claim.
"use strict";

const Runs = require("../media/runs");

const STARTING_KEY = "todoWorkers.starting"; // {<worker k>: {sessionId, entry: {id, title}}}

function attach(ctx) {
    // The state of one read: the memory moves with it, `noClaim` comes of both.
    function markNoClaim(state) {
        if (!state || !Array.isArray(state.workers)) return;
        const before = ctx.context.globalState.get(STARTING_KEY, {});
        const after = {};
        for (const worker of state.workers) {
            // without `claude agents` no session is known: keep what was remembered rather than lose it
            const kept = state.sessionsKnown ? Runs.nextMemory(before[worker.k], worker) : before[worker.k];
            if (kept) after[worker.k] = kept;
            worker.noClaim = Runs.noClaimOf(worker, kept || null);
        }
        if (JSON.stringify(before) !== JSON.stringify(after)) ctx.context.globalState.update(STARTING_KEY, after);
    }

    Object.assign(ctx, { markNoClaim });
}

module.exports = { attach };
