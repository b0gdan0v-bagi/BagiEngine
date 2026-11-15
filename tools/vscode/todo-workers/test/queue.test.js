"use strict";

const test = require("node:test");
const assert = require("node:assert");
const Runs = require("../media/runs");

test("a reorder that failed after the clear: every id the queue no longer holds is added back, in order", () => {
    assert.deepStrictEqual(Runs.idsToAddBack(["a", "b", "c", "d"], ["a", "b"]), ["c", "d"]);
    assert.deepStrictEqual(Runs.idsToAddBack(["a", "b", "c"], []), ["a", "b", "c"]);
    assert.deepStrictEqual(Runs.idsToAddBack(["a", "b"], ["b", "a"]), []);
    assert.deepStrictEqual(Runs.idsToAddBack(["a"], undefined), ["a"]);
    assert.deepStrictEqual(Runs.idsToAddBack(undefined, ["a"]), []);
});

test("an entry in progress or in review whose branch lives elsewhere is refused, naming the origin", () => {
    const show = { id: "e", status: "in progress", origin: "other-pc", base: "main", blockedBy: [] };
    assert.deepStrictEqual(Runs.queueGuard(show, ["e"]), { kind: "elsewhere", origin: "other-pc", base: "main" });
    assert.deepStrictEqual(Runs.queueGuard({ ...show, status: "review", takenOrigin: "w2", takenBase: "games/x" }, ["e"]), { kind: "elsewhere", origin: "w2", base: "games/x" });
});

test("an open entry, or one not elsewhere, is not refused", () => {
    assert.strictEqual(Runs.queueGuard({ id: "e", status: "open", origin: "other-pc", blockedBy: [] }, ["e"]), null);
    assert.strictEqual(Runs.queueGuard({ id: "e", status: "in progress", blockedBy: [] }, ["other"]), null);
    assert.strictEqual(Runs.queueGuard(null, []), null);
});

test("an open blocker is a question that names it; a closed blocker is none", () => {
    const show = { id: "e", status: "open", blockedBy: [{ id: "a", status: "in progress", title: "t" }, { id: "b", status: "closed" }, { id: "c", status: "review" }] };
    assert.deepStrictEqual(Runs.queueGuard(show, []), { kind: "blocked", blockers: [{ id: "a", status: "in progress" }, { id: "c", status: "review" }] });
    assert.strictEqual(Runs.queueGuard({ ...show, blockedBy: [{ id: "b", status: "closed" }] }, []), null);
});

const worker = (name, active, head, engine) => ({ name, active, head, engine: engine || null });

test("a loop on the base: running, head detached or on the base, no todo branch", () => {
    const workers = [
        worker("w1", true, { branch: null, commit: "abc1234" }),
        worker("w2", true, { branch: "todo/x", commit: "def5678" }),
        worker("w3", false, { branch: null, commit: "0000000" }),
        worker("w4", true, { branch: "games/match3", commit: "1111111" }),
        worker("w5", true, { branch: "main", commit: "2222222" }),
    ];
    assert.deepStrictEqual(Runs.loopsOnBase(workers, ".", "games/match3"), [{ name: "w1", commit: "abc1234" }, { name: "w4", commit: "1111111" }]);
});

test("the repository of the entry decides which head counts: engine/ in a game", () => {
    const workers = [worker("w1", true, { branch: "todo/game-work", commit: "aaa" }, { branch: null, commit: "bbb" })];
    assert.deepStrictEqual(Runs.loopsOnBase(workers, "engine", "games/match3"), [{ name: "w1", commit: "bbb" }]);
    assert.deepStrictEqual(Runs.loopsOnBase(workers, ".", "main"), []);
    assert.deepStrictEqual(Runs.loopsOnBase(workers, "other", "main"), []);
    assert.deepStrictEqual(Runs.loopsOnBase(undefined, ".", "main"), []);
});
