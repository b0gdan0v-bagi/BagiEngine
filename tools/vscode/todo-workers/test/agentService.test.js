"use strict";

const test = require("node:test");
const assert = require("node:assert");
const Runs = require("../media/runs");

// The shape of `worker.py agent status --json`.
function status(system, checkouts, ageSeconds) {
    return {
        system: { kind: "macos", text: "loaded", installed: true, running: true, disabled: false, ...system },
        rounds: { state: "running", ageSeconds },
        checkouts: checkouts || [],
        keepAwake: false,
    };
}

test("no line while the service is not installed or the status is unknown", () => {
    assert.strictEqual(Runs.agentServiceLine(null), null);
    assert.strictEqual(Runs.agentServiceLine({}), null);
    assert.strictEqual(Runs.agentServiceLine(status({ installed: false, running: false })), null);
});

test("a running service names its checkouts and the last round, and offers Stop", () => {
    const line = Runs.agentServiceLine(status({}, [{ state: "served" }, { state: "served" }, { state: "paused" }], 90));
    assert.strictEqual(line.text, "agent service running: 2 served, 1 paused, last round 2 min ago");
    assert.strictEqual(line.verb, "stop");
    assert.strictEqual(line.warn, false);
});

test("a service the owner stopped is calm and offers Start", () => {
    const line = Runs.agentServiceLine(status({ running: false, disabled: true }));
    assert.strictEqual(line.text, "agent service stopped");
    assert.strictEqual(line.verb, "start");
    assert.strictEqual(line.warn, false);
});

test("a service that is down by itself or a checkout with a problem warns", () => {
    assert.strictEqual(Runs.agentServiceLine(status({ running: false })).warn, true);
    const line = Runs.agentServiceLine(status({}, [{ state: "problem", machine: "w@mac", problem: "no token" }], 5));
    assert.strictEqual(line.warn, true);
    assert.match(line.title, /w@mac: no token/);
});

test("an entry known only from its branch never reads undefined", () => {
    assert.strictEqual(Runs.entryLabel({ id: "a", branch: "todo/a", fromBranch: true }), "a (todo/a)");
    assert.strictEqual(Runs.entryLabel({ id: "a", branch: "todo/a", status: "in progress" }), "a (in progress)");
    assert.strictEqual(Runs.entryLabel({ id: "a" }), "a");
});
