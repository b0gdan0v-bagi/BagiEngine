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
