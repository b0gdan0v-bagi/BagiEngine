// The slower views of the panel (worker.py busy, day, timeline, review-stats), read on a
// timer of their own and after a write to the store, and the actions around them: the doctor's fixes, the merge train,
// the end of the day, Dispatch, and the push to a phone (todoWorkers.pushUrl).
"use strict";

const vscode = require("vscode");
const https = require("https");
const http = require("http");
const { config, findTool, runTool, shellQuote, terminalWith } = require("./tools");
const DAY_KEY = "todoWorkers.dayStart"; // when the owner last ended the day (ms): the day view counts from it
const CACHE_KEY = "todoWorkers.insights";
const MACHINE_KEY = "todoWorkers.machine";
const HISTORY_KEY = "todoWorkers.machineHistory"; // [{at, load (1 min), mem (% used)}], one a minute
const SAMPLE_EVERY_MS = 55000;
const HISTORY_KEEP_MS = 74 * 3600 * 1000; // the longest timeline (todoWorkers.timelineHours, at most 72) and a margin // the last answer of each view, shown at once after a reload

// Each view is read on its own: `every` on the timer, `afterWrite` at the soonest after a write to the store (null: the
// store does not move it). A reading view keeps showing its last answer; only a view never read says "reading".
const PARTS = {
    day: { every: 120000, afterWrite: 30000, args: (ctx) => ["day", "--since", String(ctx.dayStart())] },
    busy: { every: 300000, afterWrite: 60000, args: () => ["busy"] },
    reviews: { every: 900000, afterWrite: 120000, args: () => ["review-stats"] },
    timeline: { every: 300000, afterWrite: null, args: () => ["timeline", "--hours", String(config().timelineHours), "--claude", config().claude] },
};

function attach(ctx) {
    const cached = ctx.context.globalState.get(CACHE_KEY, null);
    const insights = cached && cached.read ? cached : { read: {} }; // {day, busy, reviews, timeline: answer, read: {part: ms}}
    const reading = new Set();
    const writePending = new Set();

    ctx.dayStart = function dayStart() {
        const ended = ctx.context.globalState.get(DAY_KEY, 0);
        const midnight = new Date();
        midnight.setHours(0, 0, 0, 0);
        // an end of the day yesterday evening counts from then; an older one from today's midnight
        return ended && Date.now() - ended < 36 * 3600 * 1000 ? Math.min(ended, Date.now()) : midnight.getTime();
    };

    function publish() {
        ctx.view.show({ ...ctx.view.payload, insights: { ...insights, reading: [...reading] } });
    }

    async function readPart(name) {
        const tool = findTool();
        if (!tool || reading.has(name)) return;
        reading.add(name);
        publish();
        try {
            const out = await runTool(tool, [...PARTS[name].args(ctx), "--json"], 180000);
            insights[name] = JSON.parse(out);
        } catch (error) {
            // a failed read keeps the last answer; only a view never read shows the error
            if (!insights[name] || insights[name].error) insights[name] = { error: error.message };
        } finally {
            insights.read[name] = Date.now();
            reading.delete(name);
            ctx.context.globalState.update(CACHE_KEY, { ...insights, reading: undefined });
            publish();
        }
    }

    // The machine (worker.py machine: one ps and one sysctl, about 0.2 s with Python's start) on every refresh, and
    // a sample of its load and memory at most once a minute into a history of a day and more, for the timeline.
    let machine = ctx.context.globalState.get(MACHINE_KEY, null);
    let history = ctx.context.globalState.get(HISTORY_KEY, []);
    let machineReading = false;
    async function readMachine() {
        const tool = findTool();
        if (!tool || machineReading) return;
        machineReading = true;
        try {
            machine = JSON.parse(await runTool(tool, ["machine", "--json"], 30000));
            const last = history[history.length - 1];
            if (machine.load && (!last || machine.at - last.at >= SAMPLE_EVERY_MS)) {
                const mem = machine.memory;
                history = [...history.filter((h) => machine.at - h.at < HISTORY_KEEP_MS), { at: machine.at, load: machine.load[0], mem: mem ? 100 - mem.freePercent : null }];
                ctx.context.globalState.update(HISTORY_KEY, history);
            }
            ctx.context.globalState.update(MACHINE_KEY, machine);
            ctx.view.show({ ...ctx.view.payload, machine, machineHistory: history });
        } catch (error) {
            // no machine view this time: the last one stays
        } finally {
            machineReading = false;
        }
    }

    // why: "timer" (each refresh: the views past their interval), "write" (the store changed), "now" (a button: all)
    function readInsights(why) {
        const now = Date.now();
        for (const [name, part] of Object.entries(PARTS)) {
            const age = now - (insights.read[name] || 0);
            if (why === "now" || age >= part.every) {
                readPart(name);
            } else if (why === "write" && part.afterWrite !== null && !writePending.has(name)) {
                // a burst of writes (a claim, its plan, a review) makes one read, at the soonest afterWrite after the last
                writePending.add(name);
                setTimeout(() => {
                    writePending.delete(name);
                    readPart(name);
                }, Math.max(2000, part.afterWrite - age));
            }
        }
    }

    // A fix of worker.py doctor: what it does is in its text; a yes runs it.
    async function doctorFix(k, kind) {
        const tool = findTool();
        const worker = ctx.workerOf(k);
        const problem = worker && (worker.problems || []).find((p) => p.kind === kind);
        if (!tool || !problem || !problem.fix) return;
        const pick = await vscode.window.showWarningMessage(`${worker.name}: ${problem.fix}?`, { modal: true, detail: problem.text }, "Fix");
        if (!pick) return;
        try {
            const result = JSON.parse(await runTool(tool, ["doctor", String(k), "--fix", kind, "--claude", config().claude, "--json"], 120000));
            vscode.window.showInformationMessage(`${worker.name}: ${result.done}`);
            ctx.addFeed("doctor", `${worker.name}: ${result.done}`);
        } catch (error) {
            vscode.window.showErrorMessage(`${worker.name}: ${error.message}`);
        }
        ctx.refresh();
    }

    // Every ready review branch one by one (worker.py merge-train): the dialog shows the order and what stops which;
    // a yes merges until the first conflict, then Verify builds and tests the result once.
    let training = false;
    async function mergeTrain() {
        const tool = findTool();
        if (!tool || training) return;
        training = true;
        try {
            let dry;
            try {
                dry = JSON.parse(await runTool(tool, ["merge-train", "--dry-run", "--json"], 180000));
            } catch (error) {
                vscode.window.showErrorMessage(`Merge train: ${error.message}`);
                return;
            }
            if (!dry.order.length) {
                vscode.window.showInformationMessage("Merge train: nothing in review to merge here");
                return;
            }
            const lines = dry.order.map((r, i) => {
                const why = r.refused || (r.conflicts && r.conflicts.length ? `conflicts: ${r.conflicts.join(", ")}` : "") || (r.problems || []).join("; ");
                const then = r.reopen ? ", back to open for its next slice" : "";
                const pointer = r.pointer ? ", engine pointer commit" : "";
                const meets = r.conflictsWith && r.conflictsWith.length ? ` (conflicts with ${r.conflictsWith.join(", ")}: may stop the train)` : "";
                return `${i + 1}. ${r.id}: ${why || `${r.commits} commits${then}${pointer}`}${meets}`;
            });
            const detail = [...lines, "", "It stops at the first conflict or refusal; the merges before it stay. Then the main checkout is built and tested once (Verify). Nothing is pushed."].join("\n");
            const pick = await vscode.window.showWarningMessage(`Merge ${dry.order.length} review branch${dry.order.length === 1 ? "" : "es"} one by one?`, { modal: true, detail }, "Merge all + test");
            if (!pick) return;
            let result;
            try {
                result = JSON.parse(await runTool(tool, ["merge-train", "--json", "--claude", config().claude], 600000));
            } catch (error) {
                vscode.window.showErrorMessage(`Merge train: ${error.message}`);
                return;
            }
            for (const r of result.merged) ctx.addFeed("merged", `train: ${r.branch} into ${r.base} (${r.merge}), entry ${r.status || "still in review"}${r.pointer ? `, pointer ${r.pointer}` : ""}`, { entry: r.id });
            const text = `Merge train: ${result.merged.length} merged${result.stopped ? `, stopped at ${result.stopped.id}: ${result.stopped.why}` : ""}`;
            (result.stopped ? vscode.window.showWarningMessage : vscode.window.showInformationMessage)(text);
            if (result.merged.length) ctx.verifyMain(`the train of ${result.merged.length}`);
        } finally {
            training = false;
            ctx.refresh();
            readInsights("now");
        }
    }

    // The end of the day: every queue emptied (the loops that run finish their slice and start nothing new), the
    // push commands on the clipboard (pushing is the owner's), and the day view counts from now.
    async function endDay() {
        const tool = findTool();
        if (!tool) return;
        const day = insights.day && !insights.day.error ? insights.day : null;
        const running = ctx.state ? ctx.state.workers.filter((w) => w.active).map((w) => w.name) : [];
        const detail = [
            "Every worker queue is emptied: nothing starts by itself any more.",
            running.length ? `Running now: ${running.join(", ")}. They finish their slice; Stop on a card ends one at once.` : "No loop runs now.",
            day && day.pushCommands.length ? `Not pushed yet (copied to the clipboard):\n${day.pushCommands.join("\n")}` : "Nothing waits for a push (as of the last fetch).",
        ].join("\n\n");
        const pick = await vscode.window.showWarningMessage("End the day?", { modal: true, detail }, "End the day");
        if (!pick) return;
        try {
            await runTool(tool, ["queue", "--clear"], 60000);
        } catch (error) {
            vscode.window.showErrorMessage(`queue --clear: ${error.message}`);
            return;
        }
        if (day && day.pushCommands.length) await vscode.env.clipboard.writeText(day.pushCommands.join("\n"));
        await ctx.context.globalState.update(DAY_KEY, Date.now());
        ctx.addFeed("day", `the day ended: queues emptied${day ? `, ${day.merged.length} merged today` : ""}`);
        ctx.refresh();
        readInsights("now");
    }

    function copyPush() {
        const day = insights.day;
        if (!day || !day.pushCommands || !day.pushCommands.length) return;
        vscode.env.clipboard.writeText(day.pushCommands.join("\n")).then(() => vscode.window.showInformationMessage("The push commands are on the clipboard"));
    }

    // /todo-dispatch in the main checkout: it advises, asks and starts (a second click shows the same terminal).
    function dispatch() {
        const tool = findTool();
        if (tool) terminalWith("todo-dispatch", tool.cwd, `${shellQuote(config().claude)} ${shellQuote("/todo-dispatch")}`);
    }

    // A push to a phone (ntfy-style: the text POSTed to the URL, the title in a header) for what stops a worker: it
    // waits for an answer, it hit the usage limit, an entry reached review. Off while todoWorkers.pushUrl is empty.
    function pushNotify(title, text) {
        const target = config().pushUrl;
        if (!target) return;
        try {
            const url = new URL(target);
            const body = Buffer.from(text, "utf8");
            const request = (url.protocol === "http:" ? http : https).request(url, {
                method: "POST",
                headers: { Title: title.replace(/[^\x20-\x7e]/g, "?"), "Content-Type": "text/plain; charset=utf-8", "Content-Length": body.length },
                timeout: 10000,
            });
            request.on("error", () => {});
            request.on("timeout", () => request.destroy());
            request.end(body);
        } catch (error) {
            // a bad URL: nothing is sent
        }
    }

    Object.assign(ctx, { readMachine, machine: () => ({ machine, machineHistory: history }), readInsights, insights: () => ({ ...insights, reading: [...reading] }), doctorFix, mergeTrain, endDay, copyPush, dispatch, pushNotify });
}

module.exports = { attach };
