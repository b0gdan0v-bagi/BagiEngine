"use strict";

const test = require("node:test");
const assert = require("node:assert");
const Runs = require("../media/runs");

test("a refusal of queue --add: the reason, and --force only for a blocker", () => {
    assert.deepStrictEqual(
        Runs.queueRefusal("error: e not queued: blocked by a (in progress), b (open); --force queues it anyway"),
        { why: "blocked by a (in progress), b (open)", force: true }
    );
    assert.deepStrictEqual(Runs.queueRefusal("error: e not queued: taken by other-pc; --force queues it anyway"), { why: "taken by other-pc", force: false });
    assert.deepStrictEqual(Runs.queueRefusal("error: e not queued: closed; --force queues it anyway"), { why: "closed", force: false });
});

test("a reason that holds a semicolon is kept whole; the text without the prefix counts too", () => {
    assert.deepStrictEqual(Runs.queueRefusal("e not queued: blocked by a (open); b (review); --force queues it anyway"), { why: "blocked by a (open); b (review)", force: true });
});

test("a text that is no refusal gives null", () => {
    assert.strictEqual(Runs.queueRefusal("error: no worker w9 in /x"), null);
    assert.strictEqual(Runs.queueRefusal("error: not an entry id: a b"), null);
    assert.strictEqual(Runs.queueRefusal(""), null);
    assert.strictEqual(Runs.queueRefusal(undefined), null);
});

const SESSION = "w2: cd /work/w2 && CMAKE_BUILD_PARALLEL_LEVEL=4 claude --bg --name todo-w2 --model x '/todo-loop 1 next-entry --effort high report-to=main'";

test("the started entry is the id right after the slice count of the session line", () => {
    const output = ["w2: moved to the base: engine at main (abc123def)", "w2: some-id: blocked by x (open), stays queued", SESSION, "  backgrounded: abc", "", "Watch: worker.py list"].join("\n");
    assert.strictEqual(Runs.startedEntry(output), "next-entry");
    assert.strictEqual(Runs.startedEntry("w2: cd /w && claude --bg '/todo-loop 3 e_1'"), "e_1");
});

test("a start that started nothing names no entry", () => {
    assert.strictEqual(Runs.startedEntry("w2: dropped old: closed\nw2: no queued entry can be taken now, not started: a: blocked by b (open)"), null);
    assert.strictEqual(Runs.startedEntry("w2: its queue is paused (4 entries in a row), not started; `worker.py queue 2 --resume` lifts it"), null);
    assert.strictEqual(Runs.startedEntry("w2: already runs (busy), left alone"), null);
    assert.strictEqual(Runs.startedEntry("w2: cd /w && claude --bg '/todo-loop 1'"), null);
    assert.strictEqual(Runs.startedEntry(""), null);
    assert.strictEqual(Runs.startedEntry(undefined), null);
});

test("the notes of a start are the lines about the queue, not the session line", () => {
    const output = [
        "w2: dropped old: closed",
        "w2: b: blocked by a (open), stays queued",
        SESSION,
        "  backgrounded: abc",
    ].join("\n");
    assert.deepStrictEqual(Runs.queueNotes(output), ["w2: dropped old: closed", "w2: b: blocked by a (open), stays queued"]);
    assert.deepStrictEqual(Runs.queueNotes("w2: not started, the queue is paused: 4 entries in a row (a, b, c, d), e would be the next; `worker.py queue 2 --resume` goes on"), [
        "w2: not started, the queue is paused: 4 entries in a row (a, b, c, d), e would be the next; `worker.py queue 2 --resume` goes on",
    ]);
    assert.deepStrictEqual(Runs.queueNotes("  failed: boom\n  e back at the head of the queue of w2"), ["e back at the head of the queue of w2"]);
    assert.deepStrictEqual(Runs.queueNotes(undefined), []);
});

const NOW = 10_000_000;
const FREE = { k: 1, queue: ["next"], queuePaused: null, active: false, waiting: false, dirty: false, limited: null, stopped: null, entry: null, head: { branch: null }, engine: { branch: null }, session: { startedAt: NOW - 3_600_000 } };
const SETTLED = NOW - Runs.FREE_SETTLE_MS;

test("a free worker that stayed free long enough may start its queue", () => {
    assert.strictEqual(Runs.queueStartBlock(FREE, NOW, SETTLED), null);
});

test("a worker whose engine holds a todo branch with no entry known is not free, even when its session looks idle", () => {
    const w = { ...FREE, engine: { branch: "todo/data-values-core" } };
    assert.strictEqual(Runs.queueStartBlock(w, NOW, SETTLED), "holds todo/data-values-core");
    assert.strictEqual(Runs.queueStartBlock({ ...FREE, head: { branch: "todo/x" } }, NOW, SETTLED), "holds todo/x");
});

test("a held branch whose entry is in review leaves the worker free; one in progress does not", () => {
    const branch = { ...FREE, head: { branch: "todo/x" } };
    assert.strictEqual(Runs.queueStartBlock({ ...branch, entry: { id: "x", status: "review" } }, NOW, SETTLED), null);
    assert.strictEqual(Runs.queueStartBlock({ ...branch, entry: { id: "x", status: "in progress" } }, NOW, SETTLED), "holds x (in progress)");
});

test("a session started moments ago, or a worker that just turned free, waits", () => {
    assert.strictEqual(Runs.queueStartBlock({ ...FREE, session: { startedAt: NOW - 10_000 } }, NOW, SETTLED), "a session started moments ago");
    assert.strictEqual(Runs.queueStartBlock(FREE, NOW, NOW - 1000), "settling");
    assert.strictEqual(Runs.queueStartBlock(FREE, NOW, undefined), "settling");
});

test("the plain reasons come first", () => {
    assert.strictEqual(Runs.queueStartBlock({ ...FREE, queue: [] }, NOW, SETTLED), "empty queue");
    assert.strictEqual(Runs.queueStartBlock({ ...FREE, queuePaused: "by hand" }, NOW, SETTLED), "queue paused");
    assert.strictEqual(Runs.queueStartBlock({ ...FREE, active: true }, NOW, SETTLED), "a session runs");
    assert.strictEqual(Runs.queueStartBlock({ ...FREE, dirty: true }, NOW, SETTLED), "uncommitted changes");
});

test("a start the CLI marked as under way holds the queue start", () => {
    const starting = { entry: "next", at: "2026-11-16T09:00:00+00:00", ms: NOW - 1000, by: "worker.py start" };
    assert.strictEqual(Runs.queueStartBlock({ ...FREE, starting }, NOW, SETTLED), "a start is under way (next)");
    assert.strictEqual(Runs.queueStartBlock({ ...FREE, starting: null }, NOW, SETTLED), null);
});

test("a worker whose queue head is the entry it holds (the next through slice, a remarks round) is free", () => {
    const entry = { id: "next", status: "in progress" };
    assert.strictEqual(Runs.queueStartBlock({ ...FREE, entry, head: { branch: "todo/next" } }, NOW, SETTLED), null);
    assert.strictEqual(Runs.queueStartBlock({ ...FREE, entry: null, engine: { branch: "todo/next" } }, NOW, SETTLED), null);
    assert.strictEqual(Runs.queueStartBlock({ ...FREE, queue: ["other", "next"], entry, head: { branch: "todo/next" } }, NOW, SETTLED), "holds next (in progress)");
});
