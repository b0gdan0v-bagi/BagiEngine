"use strict";

const test = require("node:test");
const assert = require("node:assert");
const Runs = require("../media/runs");

const say = (text, time) => ({ time: time || "2026-11-15T10:00:00Z", kind: "say", text });
const tool = (name, text) => ({ time: "2026-11-15T10:00:00Z", kind: "tool", tool: name, text });

// A worker as `worker.py list --json --activity` gives it, a finished session without an entry.
function finished(overrides) {
    return {
        k: 1, name: "w1", active: false, waiting: false, stopped: null, limited: null, endedTurn: false, entry: null,
        session: { id: "s-1", name: "todo-w1", state: "done", status: "idle" },
        activity: { updated: 1, events: [] },
        ...overrides,
    };
}

const REMEMBERED = { sessionId: "s-1", entry: { id: "match3-level-format", title: "Level format" } };

test("the incident of w1: done, no entry, started on match3-level-format, last say names the entry it waits for", () => {
    const worker = finished({
        activity: {
            updated: 1,
            events: [
                tool("Bash", "todo.py show match3-level-format"),
                say("match3-level-format waits for match3-core-refactor (a **Waits for:** line, no dependency link). Nothing is claimed."),
                say("Stopping: waits for match3-core-refactor"),
            ],
        },
    });
    const found = Runs.noClaimOf(worker, REMEMBERED);
    assert.deepStrictEqual(found, { entry: "match3-level-format", title: "Level format", reason: "Stopping: waits for match3-core-refactor", source: "memory" });
});

test("a NOT TAKEN line names the entry and the reason, whether it ends a text or a one-line fold of it", () => {
    const multi = say("Report.\n\nNOT TAKEN match3-level-format: waits for match3-core-refactor (no dependency link)");
    assert.deepStrictEqual(Runs.parseNotTaken([multi]), { entry: "match3-level-format", reason: "waits for match3-core-refactor (no dependency link)" });
    const folded = say("Report. Offer: start it by hand. NOT TAKEN match3-level-format: waits for match3-core-refactor");
    assert.deepStrictEqual(Runs.parseNotTaken([folded]), { entry: "match3-level-format", reason: "waits for match3-core-refactor" });
    const found = Runs.noClaimOf(finished({ activity: { events: [multi] } }), REMEMBERED);
    assert.strictEqual(found.source, "line");
    assert.strictEqual(found.title, "Level format");
});

test("NOT TAKEN with - names no entry; the newest line wins; an overlong reason is cut", () => {
    assert.deepStrictEqual(Runs.parseNotTaken([say("NOT TAKEN -: nothing eligible")]), { entry: null, reason: "nothing eligible" });
    assert.strictEqual(Runs.parseNotTaken([say("NOT TAKEN a: old"), say("NOT TAKEN b: new")]).entry, "b");
    const long = Runs.parseNotTaken([say(`NOT TAKEN a: ${"x".repeat(400)}`)]);
    assert.strictEqual(long.reason.length, Runs.REASON_MAX);
    assert.strictEqual(Runs.noClaimOf(finished({ activity: { events: [say("NOT TAKEN -: nothing eligible")] } }), null).entry, null);
});

test("without a line and without a memory there is nothing to show; without a reason word the reason is null", () => {
    assert.strictEqual(Runs.noClaimOf(finished({ activity: { events: [say("Done, goodbye")] } }), null), null);
    const found = Runs.noClaimOf(finished({ activity: { events: [say("Done, goodbye")] } }), REMEMBERED);
    assert.strictEqual(found.reason, null);
    assert.strictEqual(found.entry, "match3-level-format");
});

test("a finished worker that holds an entry has no noClaim", () => {
    const worker = finished({ entry: { id: "x", status: "in progress" }, activity: { events: [say("NOT TAKEN x: nope")] } });
    assert.strictEqual(Runs.noClaimOf(worker, REMEMBERED), null);
});

test("a stopped or a limited worker has none", () => {
    const events = [say("NOT TAKEN x: nope")];
    assert.strictEqual(Runs.noClaimOf(finished({ stopped: 1700000000000, activity: { events } }), REMEMBERED), null);
    assert.strictEqual(Runs.noClaimOf(finished({ limited: { resets: "3pm" }, activity: { events } }), REMEMBERED), null);
});

test("a run still going has none; a turn that ended counts as finished", () => {
    const running = finished({ active: true, session: { id: "s-1", state: "working", status: "busy" }, activity: { events: [say("NOT TAKEN x: nope")] } });
    assert.strictEqual(Runs.noClaimOf(running, REMEMBERED), null);
    const ended = finished({ endedTurn: true, session: { id: "s-1", state: "blocked", status: "idle" }, activity: { events: [say("NOT TAKEN x: nope")] } });
    assert.strictEqual(Runs.noClaimOf(ended, null).entry, "x");
    assert.strictEqual(Runs.noClaimOf(finished({ session: null }), REMEMBERED), null);
});

test("the memory of an older session does not name the entry of a newer one", () => {
    const worker = finished({ session: { id: "s-2", state: "done" }, activity: { events: [say("Done")] } });
    assert.strictEqual(Runs.noClaimOf(worker, REMEMBERED), null);
    assert.strictEqual(Runs.nextMemory(REMEMBERED, worker), null);
});

test("nextMemory: set while a loop runs on an entry it has not claimed, kept when it ends, dropped on a claim", () => {
    const starting = { k: 1, active: true, session: { id: "s-1", state: "working" }, entry: { id: "match3-level-format", title: "Level format", starting: true } };
    const first = Runs.nextMemory(undefined, starting);
    assert.deepStrictEqual(first, REMEMBERED);
    const ended = finished();
    assert.deepStrictEqual(Runs.nextMemory(first, ended), REMEMBERED);
    const claimed = { ...starting, entry: { id: "match3-level-format", status: "in progress" } };
    assert.strictEqual(Runs.nextMemory(first, claimed), null);
    assert.strictEqual(Runs.nextMemory(first, finished({ session: null })), null);
});

test("events with no text or of an odd shape do not break it", () => {
    assert.strictEqual(Runs.parseNotTaken(undefined), null);
    assert.strictEqual(Runs.parseNotTaken([null, { kind: "say" }, { kind: "tool", text: "NOT TAKEN a: b" }, { kind: "say", text: 7 }]), null);
    assert.strictEqual(Runs.noClaimOf(finished({ activity: null }), null), null);
    assert.strictEqual(Runs.noClaimOf(finished({ activity: { events: [{ kind: "say" }] } }), REMEMBERED).reason, null);
});
