// The progress of a through entry by its stage plan: `todo.py plan list --kind stages` read once per refresh, the
// newest stage plan of each entry as `entry.stages = {name, done, total}` (media/runs.js shows it).
"use strict";

const { findTool, todoTool, runTool } = require("./tools");
const Runs = require("../media/runs");

const READ_TIMEOUT_MS = 30000;

function attach(ctx) {
    let stages = new Map();

    // An error leaves no stages: the entries show their slice plans alone.
    async function readStages() {
        const tool = findTool();
        if (!tool) return;
        try {
            stages = Runs.newestStages(JSON.parse(await runTool(todoTool(tool), ["plan", "list", "--kind", "stages", "--json"], READ_TIMEOUT_MS)));
        } catch (error) {
            stages = new Map();
        }
    }

    // Sets `stages` on the entries (in place) and returns them.
    function withStages(entries) {
        for (const entry of entries || []) {
            if (!entry) continue;
            const found = stages.get(entry.id);
            if (found) entry.stages = found;
            else delete entry.stages;
        }
        return entries;
    }

    Object.assign(ctx, { readStages, withStages });
}

module.exports = { attach };
