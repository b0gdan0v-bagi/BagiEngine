"use strict";

const test = require("node:test");
const assert = require("node:assert");
const Runs = require("../media/runs");

const BODY = "**Where:** a new folder `src/Match3/Content/Level/`.\n\n**Waits for:** the new core's `State` and `ContentPack` (`match3-core-refactor`) (the tag tree is merged: the engine's `BECore/Rules/TagTree`); the loop answers BLOCKED until they are merged.";

const store = (...rows) => new Map(rows.map(([id, status]) => [id, { id, status }]));
const STORE = store(["match3-level-format", "open"], ["match3-core-refactor", "in progress"], ["old-entry", "closed"], ["match3-level-editor", "open"]);

test("match3-level-format: the id named after Waits for with no link is found", () => {
    const entry = { id: "match3-level-format", body: BODY, blockedBy: [] };
    assert.deepStrictEqual(Runs.unlinkedWaits(entry, STORE), ["match3-core-refactor"]);
});

test("a linked, a closed and an unknown id give none", () => {
    const linked = { id: "match3-level-format", body: BODY, blockedBy: [{ id: "match3-core-refactor", status: "in progress" }] };
    assert.deepStrictEqual(Runs.unlinkedWaits(linked, STORE), []);
    const closed = { id: "x", body: "**Waits for:** old-entry", blockedBy: [] };
    assert.deepStrictEqual(Runs.unlinkedWaits(closed, STORE), []);
    const unknown = { id: "x", body: "**Waits for:** no-such-entry", blockedBy: [] };
    assert.deepStrictEqual(Runs.unlinkedWaits(unknown, STORE), []);
});

test("bold or not, a comma or an `and` list, backticks stripped, an object for the lookup", () => {
    const store2 = { a: { id: "a", status: "open" }, "b-c": { id: "b-c", status: "review" }, d: { id: "d", status: "open" } };
    const listed = { id: "x", body: "Waits for: `a`, b-c and **d**.", blockedBy: [] };
    assert.deepStrictEqual(Runs.unlinkedWaits(listed, store2), ["a", "b-c", "d"]);
    const bullet = { id: "x", body: "- **Waits for**: a", blockedBy: [] };
    assert.deepStrictEqual(Runs.unlinkedWaits(bullet, store2), ["a"]);
});

test("a body without the line, or the entry itself, or no body, gives none", () => {
    assert.deepStrictEqual(Runs.unlinkedWaits({ id: "x", body: "It waits for match3-core-refactor in prose.", blockedBy: [] }, STORE), []);
    assert.deepStrictEqual(Runs.unlinkedWaits({ id: "match3-level-format", body: "**Waits for:** match3-level-format", blockedBy: [] }, STORE), []);
    assert.deepStrictEqual(Runs.unlinkedWaits({ id: "x" }, STORE), []);
    assert.deepStrictEqual(Runs.unlinkedWaits(null, STORE), []);
    assert.deepStrictEqual(Runs.unlinkedWaits({ id: "x", body: "**Waits for:** a" }, null), []);
});

test("an id named twice shows once", () => {
    const entry = { id: "x", body: "**Waits for:** match3-core-refactor\n**Waits for:** match3-core-refactor, match3-level-editor", blockedBy: [] };
    assert.deepStrictEqual(Runs.unlinkedWaits(entry, STORE), ["match3-core-refactor", "match3-level-editor"]);
});
