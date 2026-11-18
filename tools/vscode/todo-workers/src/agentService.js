// The PC's agent service (`worker.py agent`, one process for every checkout of the machine): the state the panel shows
// beside the workers and its Start / Stop.
"use strict";

const vscode = require("vscode");
const { findTool, runTool } = require("./tools");

const READ_TIMEOUT_MS = 15000;
const CONTROL_TIMEOUT_MS = 60000;
const CONFIRM = {
    start: { button: "Start", text: "Start the agent service of this PC?", detail: "It serves every checkout on its list." },
    stop: { button: "Stop", text: "Stop the agent service of this PC?", detail: "It serves no checkout of this PC and stays stopped, also after a new login, until Start. Running worker sessions go on." },
};

function attach(ctx) {
    let reading = false;

    async function readAgentService() {
        const tool = findTool();
        if (!tool || reading) return;
        reading = true;
        try {
            const agentService = JSON.parse(await runTool(tool, ["agent", "status", "--json"], READ_TIMEOUT_MS));
            ctx.view.show({ ...ctx.view.payload, agentService });
        } catch (error) {
            // a CLI without the service or a failed read: no line rather than a stale one
            ctx.view.show({ ...ctx.view.payload, agentService: null });
        } finally {
            reading = false;
        }
    }

    async function controlAgentService(verb) {
        const confirm = CONFIRM[verb];
        const tool = findTool();
        if (!confirm || !tool) return;
        const pick = await vscode.window.showWarningMessage(confirm.text, { modal: true, detail: confirm.detail }, confirm.button);
        if (pick !== confirm.button) return;
        try {
            await runTool(tool, ["agent", verb], CONTROL_TIMEOUT_MS);
        } catch (error) {
            vscode.window.showErrorMessage(`worker.py agent ${verb}: ${error.message}`);
        }
        await readAgentService();
    }

    Object.assign(ctx, { readAgentService, controlAgentService });
}

module.exports = { attach };
