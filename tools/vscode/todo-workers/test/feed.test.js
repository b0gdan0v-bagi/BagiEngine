"use strict";

const test = require("node:test");
const assert = require("node:assert");
const Module = require("node:module");

// feed.js runs inside VS Code: only what it touches here is stood in for.
const vscodeStub = {
    workspace: { getConfiguration: () => ({ get: (key, fallback) => (key === "notify" ? false : fallback) }) },
    window: {},
};
const load = Module._load;
Module._load = (request, ...rest) => (request === "vscode" ? vscodeStub : load(request, ...rest));
const Feed = require("../src/feed");
Module._load = load;

function worker(fields) {
    return { name: "w1", k: 1, active: false, waiting: false, limited: null, noClaim: null, stopped: false, entry: null, ...fields };
}

// The feed and the pushes of one change from `before` to `after`.
function changes(before, after) {
    const items = [];
    const pushes = [];
    const ctx = {
        context: { globalState: { get: () => [], update: (key, value) => items.splice(0, items.length, ...value) } },
        view: { show() {}, payload: {} },
        pushNotify: (title) => pushes.push(title),
    };
    Feed.attach(ctx);
    ctx.state = { review: [], workers: [], store: "ok", sessionsKnown: true, ...before };
    ctx.notifyChanges();
    ctx.state = { review: [], workers: [], store: "ok", sessionsKnown: true, ...after };
    ctx.notifyChanges();
    return { texts: items.map((item) => item.text), pushes };
}

test("an entry reaching review goes to the feed and is not pushed", () => {
    const { texts, pushes } = changes({}, { review: [{ id: "a", branch: "todo/a", statusNote: "done" }] });
    assert.deepStrictEqual(texts, ["a reached review: done"]);
    assert.deepStrictEqual(pushes, []);
});

test("a worker that waits or hit the limit is still pushed", () => {
    const { pushes } = changes({ workers: [worker()] }, { workers: [worker({ waiting: true, limited: { resets: "10pm" } })] });
    assert.deepStrictEqual(pushes, ["Todo: w1 waits for you", "Todo: w1 hit the usage limit"]);
});

test("the end of a loop on an entry known only from its branch names the branch", () => {
    const entry = { id: "a", branch: "todo/a", fromBranch: true };
    const { texts } = changes({ workers: [worker({ active: true, entry })] }, { workers: [worker({ entry })] });
    assert.deepStrictEqual(texts, ["w1: the loop ended (a (todo/a))"]);
});
