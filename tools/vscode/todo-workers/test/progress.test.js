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
    assert.deepStrictEqual(Runs.progressOf(CORE_REFACTOR), { percent: 100, whole: false, text: "~100% · slice", detail: "this slice 36/36 (no stage plan: the percent covers only what is planned so far)" });
    assert.strictEqual(Runs.progressText({ ...CORE_REFACTOR, planProgress: { done: 9, total: 36 } }), "~25% · slice");
});

test("a through entry with a stage plan counts the stages approved plus the share of this slice", () => {
    // 1 of 2 stages approved, slice 2 done but not approved yet: never 100 before the last stage is ticked.
    assert.deepStrictEqual(Runs.progressOf(EDITOR_CONSOLE), { percent: 95, whole: true, text: "~95%", detail: "stage 1/2 · this slice 39/39" });
    // (2 + 12/30) / 3 = 0.8
    const shown = Runs.progressOf({ ...EDITOR_CONSOLE, planProgress: { done: 12, total: 30 }, slices: { count: 3, done: 2, total: 3, by: "stages" } });
    assert.deepStrictEqual(shown, { percent: 80, whole: true, text: "~80%", detail: "stage 2/3 · this slice 12/30" });
    // (1 + 34/76) / 2 = 0.72 -> 70
    assert.strictEqual(Runs.progressText({ ...EDITOR_CONSOLE, planProgress: { done: 34, total: 76 } }), "~70%");
});

test("an entry cut slice by slice with a stage plan counts the whole entry too", () => {
    const entry = { slicing: null, slices: { count: 1, done: 1, total: 4, by: "stages" }, stagesPlan: "todo-loop-y-stages.md", planShown: "todo-loop-y-2.md", planProgress: { done: 10, total: 20 } };
    // (1 + 10/20) / 4 = 0.375 -> 40
    assert.deepStrictEqual(Runs.progressOf(entry), { percent: 40, whole: true, text: "~40%", detail: "stage 1/4 · this slice 10/20" });
});

test("every stage approved is 100", () => {
    assert.strictEqual(Runs.progressText({ ...EDITOR_CONSOLE, slices: { count: 2, done: 2, total: 2, by: "stages" }, planProgress: { done: 3, total: 39 } }), "~100%");
});

test("a through entry whose shown plan is the stage plan has no slice to count", () => {
    assert.deepStrictEqual(Runs.progressOf(STAGE_PLAN_SHOWN), { percent: 0, whole: true, text: "~0%", detail: "stage 0/1" });
});

test("a through entry that has a stage plan and no slice plan yet", () => {
    const shown = Runs.progressOf({ slicing: "through", slices: { count: 1, done: 1, total: 3, by: "stages" } });
    assert.deepStrictEqual(shown, { percent: 35, whole: true, text: "~35%", detail: "stage 1/3" });
});

test("an entry cut slice by slice sums its slice plans, a single plan counts its boxes", () => {
    const all = Runs.progressOf({ slicing: "none", slices: { count: 3, done: 72, total: 110, by: "slices" }, planProgress: { done: 5, total: 36 } });
    assert.deepStrictEqual(all, { percent: 65, whole: false, text: "~65% · slice", detail: "3 slices 72/110 · this slice 5/36 (no stage plan: the percent covers only what is planned so far)" });
    assert.deepStrictEqual(Runs.progressOf({ slicing: null, planProgress: { done: 34, total: 76 } }), { percent: 45, whole: false, text: "~45% · slice", detail: "plan 34/76 (no stage plan: the percent covers only what is planned so far)" });
    assert.strictEqual(Runs.progressDetail({ slices: { count: 1, done: 1, total: 4, by: "slices" }, planProgress: { done: 1, total: 4 } }), "plan 1/4 (no stage plan: the percent covers only what is planned so far)");
});

test("the percent is rounded to 5, at least 5 once a box is ticked, at most 95 before the last", () => {
    assert.strictEqual(Runs.progressText({ planProgress: { done: 1, total: 100 } }), "~5% · slice");
    assert.strictEqual(Runs.progressText({ planProgress: { done: 0, total: 100 } }), "~0% · slice");
    assert.strictEqual(Runs.progressText({ planProgress: { done: 99, total: 100 } }), "~95% · slice");
    assert.strictEqual(Runs.progressText({ planProgress: { done: 100, total: 100 } }), "~100% · slice");
});

test("an entry with no plan shows nothing", () => {
    assert.strictEqual(Runs.progressText({ slicing: "through" }), "");
    assert.strictEqual(Runs.progressDetail({ slicing: "through" }), "");
    assert.strictEqual(Runs.progressOf({ planProgress: { done: 0, total: 0 } }), null);
    assert.strictEqual(Runs.progressOf(null), null);
});

test("the graph box of a held entry shows the percent, its tooltip the counts", () => {
    const worker = { k: 1, name: "w1", active: true, session: { state: "working", status: "busy" }, entry: { ...EDITOR_CONSOLE, status: "in progress" } };
    const svg = Graph.toSvg([worker]);
    assert.ok(svg.includes("in progress · ~95%"));
    assert.ok(svg.includes("~95% (stage 1/2 · this slice 39/39)"));
});
