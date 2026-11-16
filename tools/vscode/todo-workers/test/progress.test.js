"use strict";

const test = require("node:test");
const assert = require("node:assert");
const Runs = require("../media/runs");
const Graph = require("../media/graph");

// The entries as the store gives them (`slices`, `stagesPlan`, `planShown`, `planProgress`).
// match3-core-refactor: through, four slice plans (108 of 110 lines), no stage plan, the plan of this slice 36/36.
const CORE_REFACTOR = { id: "match3-core-refactor", slicing: "through", slices: { count: 4, done: 108, total: 110, by: "slices" }, stagesPlan: null, planShown: "todo-loop-match3-core-refactor-4.md", planProgress: { done: 36, total: 36 } };
// editor-console: through, a stage plan of 2 stages (1 done), the plan of slice 2 shown.
const EDITOR_CONSOLE = { id: "editor-console", slicing: "through", slices: { count: 2, done: 1, total: 2, by: "stages" }, stagesPlan: "todo-loop-editor-console-stages.md", planShown: "todo-loop-editor-console-s2.md", planProgress: { done: 39, total: 39 } };
// The stage plan is the plan shown: its boxes are not a slice.
const STAGE_PLAN_SHOWN = { id: "x", slicing: "through", slices: { count: 1, done: 0, total: 1, by: "stages" }, stagesPlan: "todo-loop-x-stages.md", planShown: "todo-loop-x-stages.md", planProgress: { done: 0, total: 1 } };

test("a through entry without a stage plan shows this slice only, never the sum of the old slice plans", () => {
    assert.strictEqual(Runs.progressText(CORE_REFACTOR), "this slice 36/36");
    assert.deepStrictEqual(Runs.progressOf(CORE_REFACTOR), { done: 36, total: 36, text: "this slice 36/36" });
});

test("a through entry with a stage plan shows the stage and its slice, and the bar is the stages", () => {
    assert.strictEqual(Runs.progressText(EDITOR_CONSOLE), "stage 1/2 · this slice 39/39");
    const shown = Runs.progressOf({ ...EDITOR_CONSOLE, planProgress: { done: 12, total: 30 }, slices: { count: 3, done: 2, total: 3, by: "stages" } });
    assert.deepStrictEqual(shown, { done: 2, total: 3, text: "stage 2/3 · this slice 12/30" });
});

test("a through entry whose shown plan is the stage plan has no slice to show", () => {
    assert.deepStrictEqual(Runs.progressOf(STAGE_PLAN_SHOWN), { done: 0, total: 1, text: "stage 0/1" });
});

test("a through entry that has a stage plan and no slice plan yet", () => {
    assert.strictEqual(Runs.progressText({ slicing: "through", slices: { count: 1, done: 0, total: 3, by: "stages" } }), "stage 0/3");
});

test("an entry cut slice by slice keeps the old wording", () => {
    assert.strictEqual(Runs.progressText({ slicing: "none", slices: { count: 3, done: 72, total: 110, by: "slices" }, planProgress: { done: 5, total: 36 } }), "3 slices 72/110 · this slice 5/36");
    assert.strictEqual(Runs.progressText({ slicing: null, planProgress: { done: 5, total: 36 } }), "plan 5/36");
    assert.strictEqual(Runs.progressText({ slices: { count: 1, done: 1, total: 4, by: "slices" }, planProgress: { done: 1, total: 4 } }), "plan 1/4");
});

test("an entry with no plan shows nothing", () => {
    assert.strictEqual(Runs.progressText({ slicing: "through" }), "");
    assert.strictEqual(Runs.progressOf({ planProgress: { done: 0, total: 0 } }), null);
    assert.strictEqual(Runs.progressOf(null), null);
});

test("the graph box of a held entry carries the same progress words", () => {
    const worker = { k: 1, name: "w1", active: true, session: { state: "working", status: "busy" }, entry: { ...EDITOR_CONSOLE, status: "in progress" } };
    const svg = Graph.toSvg([worker]);
    assert.ok(svg.includes("stage 1/2 · this slice 39/39"));
});
