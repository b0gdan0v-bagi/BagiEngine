// What the panel says about a run, as pure functions of the rows of `worker.py list --json` (and of the entries of
// `todo.py list --json`): the words of a run that took nothing, the progress of an entry, the reading of what the
// queue commands print. Shared by the extension (src/) and the webviews, and driven by node in test/ without VS Code.
(function (root, factory) {
    if (typeof module === "object" && module.exports) module.exports = factory();
    else root.TodoRuns = factory();
})(typeof self !== "undefined" ? self : this, function () {
    const NO_REASON = "no reason said: Log shows the run";

    // The progress an entry shows, as the bar's fill and its words, from the `slices` and `planProgress` of the store.
    // A through entry is one branch over many slices: `slices.by` is "stages" when it has a stage plan (the whole is
    // the stages and the slice plan is "this slice"), else its slice plans of the past say nothing about it. An entry
    // cut slice by slice shows its slice plans together. A shown plan that is the stage plan has no slice to show.
    function progressOf(entry) {
        if (!entry) return null;
        const slices = entry.slices || null;
        const p = entry.planProgress && entry.planProgress.total ? entry.planProgress : null;
        const stages = slices && slices.by === "stages" && slices.total ? slices : null;
        const all = slices && slices.by !== "stages" && slices.count > 1 && slices.total ? slices : null;
        const sliceShown = Boolean(entry.planShown) && entry.planShown === entry.stagesPlan ? null : p;
        const slice = sliceShown ? `this slice ${sliceShown.done}/${sliceShown.total}` : "";
        if (entry.slicing === "through") {
            if (stages) return { done: stages.done, total: stages.total, text: [`stage ${stages.done}/${stages.total}`, slice].filter(Boolean).join(" · ") };
            return sliceShown ? { done: sliceShown.done, total: sliceShown.total, text: slice } : null;
        }
        if (all) return { done: all.done, total: all.total, text: `${all.count} slices ${all.done}/${all.total}${sliceShown ? ` · ${slice}` : ""}` };
        return p ? { done: p.done, total: p.total, text: `plan ${p.done}/${p.total}` } : null;
    }

    function progressText(entry) {
        const shown = progressOf(entry);
        return shown ? shown.text : "";
    }

    // The entries whose text says they wait for others while the store has no link, from the answer of `worker.py doctor
    // --json`: its `store` rows of the kind `waits-unlinked` (`entry`, `missing`) as `{entry id: [missing ids]}`. The
    // CLI decides what counts; any other answer (an error object, null, no `store`) gives none.
    function unlinkedWaitsOf(doctor) {
        const found = {};
        const rows = doctor && Array.isArray(doctor.store) ? doctor.store : [];
        for (const row of rows) {
            if (row && row.kind === "waits-unlinked" && row.entry && Array.isArray(row.missing) && row.missing.length) found[row.entry] = row.missing;
        }
        return found;
    }

    // What `worker.py queue <k> --add <id>` refuses with, as `runTool` gives it (the last line of stderr):
    // `error: <id> not queued: <why>; --force queues it anyway`. `force` is true when the owner may overrule it, which
    // only a blocker allows: an entry taken elsewhere or closed is dropped by the next start even when forced. The
    // reason may hold `;`, so the match runs to the last `; --force`. null: the text is no refusal.
    const REFUSAL = /^(?:error: )?\S+ not queued: (.+); --force queues it anyway\s*$/;
    const BLOCKED = "blocked by";

    function queueRefusal(message) {
        const found = REFUSAL.exec(String(message || ""));
        return found ? { why: found[1], force: found[1].startsWith(BLOCKED) } : null;
    }

    // The entry `worker.py start <k> --queued-only` started, from the line it prints for the session:
    // `w<k>: cd <path> && ... claude --bg ... '/todo-loop <N> <id> ...'`. null when it started none (a paused or empty
    // queue, nothing takeable, a session that runs already).
    const STARTED_LINE = /^w\d+: cd .*'\/todo-loop\s+\d+\s+([A-Za-z0-9][A-Za-z0-9_-]*)[\s']/;

    function startedEntry(output) {
        for (const line of String(output || "").split("\n")) {
            const found = STARTED_LINE.exec(line.trim());
            if (found) return found[1];
        }
        return null;
    }

    // The lines of `start --queued-only` that say what it did to the queue: an entry dropped with its reason, one that
    // stays queued, a pause, nothing takeable, an entry put back after a failed start. Not the line of the session.
    const QUEUE_LINE = /\bdropped \S+: |, stays queued$|queue is paused|not started|no queued entry|back at the head/;

    function queueNotes(output) {
        return String(output || "")
            .split("\n")
            .map((line) => line.trim())
            .filter((line) => QUEUE_LINE.test(line) && !/^w\d+: cd /.test(line));
    }


    // Why the panel must not start the next entry of a worker's queue now, or null when it may. `worker.py list`
    // names one session per worker (the newest of the name todo-w<k>), so a live loop can hide behind a newer session
    // that ended at once, and a start from the command line is not listed for a few seconds: the session state alone
    // does not prove the folder free. A checked-out todo/* branch whose entry is not known to be in review means a
    // loop holds the folder; a session started less than START_GRACE_MS ago may still be claiming; and a worker has to
    // look free for FREE_SETTLE_MS on end before the start, so one refresh in the middle of a start does not count.
    const START_GRACE_MS = 3 * 60 * 1000;
    const FREE_SETTLE_MS = 45 * 1000;

    function heldBranch(w) {
        for (const repo of [w.head, w.engine]) {
            const branch = repo && repo.branch;
            if (typeof branch === "string" && branch.startsWith("todo/")) return branch;
        }
        return null;
    }

    function queueStartBlock(w, now, freeSince) {
        if (!w.queue || !w.queue.length) return "empty queue";
        if (w.queuePaused) return "queue paused";
        if (w.active || w.waiting) return "a session runs";
        if (w.dirty) return "uncommitted changes";
        if (w.limited) return "usage limit";
        if (w.stopped) return "stopped";
        // worker.py start writes a `starting` mark into the queue file before its session shows in `list` (CLI 2026-11-16)
        if (w.starting) return `a start is under way${w.starting.entry ? ` (${w.starting.entry})` : ""}`;
        if (w.entry && w.entry.status !== "review") return `holds ${w.entry.id} (${w.entry.status})`;
        const branch = heldBranch(w);
        if (branch && !(w.entry && w.entry.status === "review")) return `holds ${branch}`;
        const started = w.session && Number(w.session.startedAt);
        if (started && now - started < START_GRACE_MS) return "a session started moments ago";
        if (freeSince === undefined || freeSince === null || now - freeSince < FREE_SETTLE_MS) return "settling";
        return null;
    }

    return { NO_REASON, progressOf, progressText, unlinkedWaitsOf, queueRefusal, startedEntry, queueNotes, queueStartBlock, START_GRACE_MS, FREE_SETTLE_MS };
});
