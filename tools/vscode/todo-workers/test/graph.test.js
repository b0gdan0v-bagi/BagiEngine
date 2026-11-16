"use strict";

const test = require("node:test");
const assert = require("node:assert");
const Graph = require("../media/graph");

const FINISHED = { state: "done", status: "idle" };

function worker(overrides) {
    return { k: 1, name: "w1", active: false, session: FINISHED, entry: null, queue: [], ...overrides };
}

// Every box of the tree with its class, texts and tooltip.
function boxes(workers) {
    return Graph.build(workers).tree.kids.find((k) => k.attrs.class === "g-boxes").kids;
}

const classOf = (box) => box.attrs.class;
const textsOf = (box) => box.kids.filter((k) => k.tag === "text").map((k) => k.text);
const tipOf = (box) => (box.kids.find((k) => k.tag === "title") || {}).text;

// `worker.noClaim` as `worker.py list --json --store` gives it.
const TOOK_NOTHING = { entry: "match3-level-format", reason: "waits for match3-core-refactor" };

test("a worker that took nothing is its own state, with the tried entry in place of the empty box", () => {
    const [wbox, nbox] = boxes([worker({ noClaim: TOOK_NOTHING })]);
    assert.match(classOf(wbox), /sess-noclaim/);
    assert.ok(textsOf(wbox).includes("took nothing"));
    assert.match(classOf(nbox), /g-noclaim/);
    const [id, sub] = textsOf(nbox);
    assert.strictEqual(id, "match3-level-format");
    assert.ok(sub.startsWith("not taken · waits for"), sub); // the box is small: the tooltip holds the whole reason
    assert.match(tipOf(nbox), /said by the loop/);
    assert.match(tipOf(nbox), /waits for match3-core-refactor/);
    assert.deepStrictEqual(nbox.act, { type: "entry", entry: "match3-level-format", worker: 1, name: "w1", held: false });
    const svg = Graph.toSvg([worker({ noClaim: TOOK_NOTHING })]);
    assert.ok(!svg.includes(">no entry<"));
    assert.ok(!svg.includes(">done<"));
});

test("the tooltip of an entry seen only at start (null reason) names that source", () => {
    const [, nbox] = boxes([worker({ noClaim: { ...TOOK_NOTHING, reason: null } })]);
    assert.match(tipOf(nbox), /seen at start/);
    assert.ok(!/NOT TAKEN line/.test(tipOf(nbox)));
    assert.match(textsOf(nbox)[1], /^not taken · no reason/);
    assert.match(tipOf(nbox), /no reason said: Log shows the run/);
});

test("NOT TAKEN - shows that nothing was eligible and cannot be clicked", () => {
    const [, nbox] = boxes([worker({ noClaim: { entry: null, reason: "every entry is busy" } })]);
    assert.strictEqual(textsOf(nbox)[0], "nothing eligible");
    assert.strictEqual(nbox.act, undefined);
});

test("a stopped, a limited and an empty worker still render as before", () => {
    const stopped = boxes([worker({ stopped: 1700000000000 })]);
    assert.match(classOf(stopped[0]), /sess-stopped/);
    assert.match(classOf(stopped[1]), /g-ghost/);
    const limited = boxes([worker({ limited: { resets: "3pm" } })]);
    assert.match(classOf(limited[0]), /sess-limited/);
    assert.match(classOf(limited[1]), /g-ghost/);
    const empty = boxes([worker({ session: null })]);
    assert.match(classOf(empty[0]), /sess-none/);
    assert.deepStrictEqual(textsOf(empty[1]), ["no entry"]);
    assert.ok(Graph.toSvg([worker({})]).includes(">no entry<"));
});

test("a worker that holds an entry draws it, not the noClaim box", () => {
    const held = worker({ entry: { id: "x", status: "in progress" }, noClaim: TOOK_NOTHING });
    const all = boxes([held]);
    assert.ok(all.some((b) => /g-held/.test(classOf(b))));
    assert.ok(!all.some((b) => /g-noclaim/.test(classOf(b))));
});

test("a CLI without noClaim (an older loader) draws the empty box and no error", () => {
    const all = boxes([worker({ noClaim: undefined })]);
    assert.match(classOf(all[0]), /sess-done/);
    assert.match(classOf(all[1]), /g-ghost/);
});
