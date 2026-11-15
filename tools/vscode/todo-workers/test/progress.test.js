"use strict";

const test = require("node:test");
const assert = require("node:assert");
const Runs = require("../media/runs");
const Graph = require("../media/graph");

// match3-core-refactor as the panel reads it: through, many old slice plans (72 of 110 lines), the plan of this slice 0/36.
const CORE_REFACTOR = { id: "match3-core-refactor", slicing: "through", slices: { count: 3, done: 72, total: 110 }, planProgress: { done: 0, total: 36 } };

test("a through entry without a stage plan shows this slice only, never the sum of the old slice plans", () => {
    assert.strictEqual(Runs.progressText(CORE_REFACTOR), "this slice 0/36");
    assert.deepStrictEqual(Runs.progressOf(CORE_REFACTOR), { done: 0, total: 36, text: "this slice 0/36" });
});

test("a through entry with a stage plan shows the stage and its slice, and the bar is the stages", () => {
    const entry = { ...CORE_REFACTOR, planProgress: { done: 12, total: 30 }, stages: { name: "todo-loop-x-stages.md", done: 2, total: 3 } };
    assert.strictEqual(Runs.progressText(entry), "stage 2/3 · this slice 12/30");
    const shown = Runs.progressOf(entry);
    assert.strictEqual(shown.done, 2);
    assert.strictEqual(shown.total, 3);
});

test("a through entry that has a stage plan and no slice plan yet", () => {
    assert.strictEqual(Runs.progressText({ slicing: "through", stages: { done: 0, total: 3 } }), "stage 0/3");
});

test("an entry cut slice by slice keeps the old wording", () => {
    assert.strictEqual(Runs.progressText({ slicing: "none", slices: { count: 3, done: 72, total: 110 }, planProgress: { done: 5, total: 36 } }), "3 slices 72/110 · this slice 5/36");
    assert.strictEqual(Runs.progressText({ slicing: null, planProgress: { done: 5, total: 36 } }), "plan 5/36");
    assert.strictEqual(Runs.progressText({ slices: { count: 1, done: 1, total: 4 }, planProgress: { done: 1, total: 4 } }), "plan 1/4");
});

test("an entry with no plan shows nothing", () => {
    assert.strictEqual(Runs.progressText({ slicing: "through" }), "");
    assert.strictEqual(Runs.progressOf({ planProgress: { done: 0, total: 0 } }), null);
    assert.strictEqual(Runs.progressOf(null), null);
});

test("the newest stage plan of each entry is kept; a plan without lines is left out", () => {
    const stages = Runs.newestStages([
        { name: "a-old.md", entry: "a", progress: { done: 1, total: 3 }, updatedAt: "2026-11-01T00:00:00+00:00" },
        { name: "a-new.md", entry: "a", progress: { done: 2, total: 3 }, updatedAt: "2026-11-02T00:00:00+00:00" },
        { name: "b.md", entry: "b", progress: { done: 0, total: 0 }, updatedAt: "2026-11-02T00:00:00+00:00" },
        { name: "orphan.md", entry: null, progress: { done: 1, total: 1 } },
    ]);
    assert.deepStrictEqual([...stages], [["a", { name: "a-new.md", done: 2, total: 3 }]]);
    assert.strictEqual(Runs.newestStages(undefined).size, 0);
});

test("the graph box of a held entry carries the same progress words", () => {
    const worker = { k: 1, name: "w1", active: true, session: { state: "working", status: "busy" }, entry: { ...CORE_REFACTOR, status: "in progress" } };
    const svg = Graph.toSvg([worker]);
    assert.ok(svg.includes("this slice 0/36"));
    assert.ok(!svg.includes("72/110"));
});
