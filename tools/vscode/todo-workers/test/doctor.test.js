"use strict";

const test = require("node:test");
const assert = require("node:assert");
const Runs = require("../media/runs");

// The shape of `worker.py doctor --json` with no worker named: a list of problems per worker and the store's.
const ANSWER = {
    w1: [{ kind: "sessions", fix: "sessions", text: "two loops run in one folder" }],
    w2: [],
    store: [
        {
            kind: "waits-unlinked",
            entry: "match3-level-format",
            fix: null,
            missing: ["match3-core-refactor"],
            text: "match3-level-format waits for match3-core-refactor by its body, the store has no link: todo.py dep add match3-level-format match3-core-refactor",
        },
        { kind: "store", fix: null, text: "the entries could not be read: boom" },
        { kind: "waits-unlinked", entry: "b", fix: null, missing: ["x", "y"], text: "b waits for x, y" },
    ],
};

test("the rows of kind waits-unlinked give the missing ids per entry", () => {
    assert.deepStrictEqual(Runs.unlinkedWaitsOf(ANSWER), { "match3-level-format": ["match3-core-refactor"], b: ["x", "y"] });
});

test("an answer without them, an error and no answer give none", () => {
    assert.deepStrictEqual(Runs.unlinkedWaitsOf({ w1: [], store: [] }), {});
    assert.deepStrictEqual(Runs.unlinkedWaitsOf({ w1: [] }), {});
    assert.deepStrictEqual(Runs.unlinkedWaitsOf({ error: "worker.py doctor: boom" }), {});
    assert.deepStrictEqual(Runs.unlinkedWaitsOf({ store: [{ kind: "waits-unlinked", entry: "a", missing: [] }, { kind: "waits-unlinked", missing: ["x"] }] }), {});
    assert.deepStrictEqual(Runs.unlinkedWaitsOf(null), {});
    assert.deepStrictEqual(Runs.unlinkedWaitsOf(undefined), {});
});
