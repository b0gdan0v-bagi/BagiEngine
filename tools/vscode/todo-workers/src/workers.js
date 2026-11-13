// The actions on a worker: its session (attach, logs, stop, relaunch), its folder (update, fast start), its queue
// (queue, unqueue, the auto-start of the next entry), starting an entry in a free worker, the engine pointer commit.
"use strict";

const vscode = require("vscode");
const fs = require("fs");
const path = require("path");
const { config, findTool, runTool, runGit, branchId, planFile, shellQuote, toolEnv } = require("./tools");

function attach(ctx) {
    const busy = ctx.busy;

    async function overlapWarning(id) {
        const tool = findTool();
        try {
            const result = JSON.parse(await runTool(tool, ["overlap", id, "--json"], 90000));
            return result.overlaps.map((o) => `${o.id} (${o.status}): ${o.files.join(", ")}`);
        } catch (error) {
            return [];
        }
    }

    // --- queues: the next entries of each worker, started by the panel as soon as the worker is free ----------
    async function queueEntry(id, k) {
        const tool = findTool();
        if (!tool || !id || !/^[A-Za-z0-9_-]+$/.test(id)) return;
        if (!ctx.state) await ctx.refresh();
        const state = ctx.state;
        if (!state || !state.workers.length) return;
        let worker = k === undefined ? null : state.workers.find((w) => w.k === k);
        const overlaps = await overlapWarning(id);
        if (!worker) {
            const label = (w) => `${w.name}${w.entry ? ` (now ${w.entry.id})` : ""}${w.queue && w.queue.length ? `, ${w.queue.length} queued` : ""}`;
            const detail = [
                "The panel starts /todo-loop 1 " + id + " there as soon as that worker is free (no session running, no uncommitted changes, its entry in review or none).",
                ...(overlaps.length ? ["", "Busy entries name the same files (the loop will see them as busy):", ...overlaps.map((o) => `  ${o}`)] : []),
            ].join("\n");
            const picked = await vscode.window.showInformationMessage(`Queue ${id} for which worker?`, { modal: true, detail }, ...state.workers.map(label));
            worker = state.workers.find((w) => label(w) === picked);
        }
        if (!worker) return;
        try {
            await runTool(tool, ["queue", String(worker.k), "--add", id], 30000);
            ctx.addFeed("queued", `${id} queued for ${worker.name}${overlaps.length ? ` (meets ${overlaps.length} busy entr${overlaps.length === 1 ? "y" : "ies"})` : ""}`, { entry: id });
        } catch (error) {
            vscode.window.showErrorMessage(`worker.py queue: ${error.message}`);
        }
        await ctx.refresh();
    }

    async function unqueueEntry(id, k) {
        const tool = findTool();
        if (!tool || !id) return;
        try {
            await runTool(tool, ["queue", String(k), "--remove", id], 30000);
        } catch (error) {
            vscode.window.showErrorMessage(`worker.py queue: ${error.message}`);
        }
        await ctx.refresh();
    }

    // A new order of one worker's queue (drag and drop in the Todo list): cleared and added again in that order.
    async function reorderQueue(k, ids) {
        const tool = findTool();
        if (!tool || !Array.isArray(ids) || !ids.every((id) => /^[A-Za-z0-9_-]+$/.test(id))) return;
        try {
            await runTool(tool, ["queue", String(k), "--clear"], 30000);
            for (const id of ids) await runTool(tool, ["queue", String(k), "--add", id], 30000);
        } catch (error) {
            vscode.window.showErrorMessage(`worker.py queue: ${error.message}`);
        }
        await ctx.refresh();
    }

    // A free worker with a queue gets its next entry; one start per refresh, so the loops do not choose at once.
    let autoStarting = false;
    async function autoStartQueues() {
        const c = config();
        const tool = findTool();
        const state = ctx.state;
        if (!c.autoStartQueue || !tool || !state || autoStarting) return;
        const worker = state.workers.find(
            (w) => w.queue && w.queue.length && !w.active && !w.dirty && !w.limited && !w.stopped && !busy.has(w.k) && (!w.entry || w.entry.status === "review")
        );
        if (!worker) return;
        autoStarting = true;
        busy.add(worker.k);
        try {
            const popped = JSON.parse(await runTool(tool, ["queue", String(worker.k), "--pop", "--json"], 30000)).popped;
            if (!popped) return;
            const loopArgs = [popped, c.loopArgs].filter(Boolean).join(" ");
            const args = ["start", String(worker.k), "--delay", "0", "--slices", "1", "--loop-args", loopArgs, "--claude", c.claude];
            if (c.reportTo) args.push("--report-to", c.reportTo);
            await runTool(tool, args, 90000);
            ctx.addFeed("started", `${worker.name} took ${popped} from its queue`, { entry: popped });
            if (c.notify) vscode.window.showInformationMessage(`Todo: ${worker.name} started ${popped} from its queue`);
        } catch (error) {
            ctx.addFeed("error", `${worker.name}: the queued start failed: ${error.message}`);
        } finally {
            busy.delete(worker.k);
            autoStarting = false;
            ctx.refresh();
        }
    }

    // --- the engine pointer: the one commit of the game that moves engine/ to what its checkout holds -----------
    async function commitPointer() {
        const tool = findTool();
        const p = ctx.state && ctx.state.pointer;
        if (!tool || !p || !p.moved) return;
        const subjects = (await runGit(path.join(tool.cwd, "engine"), ["log", "--format=%s", `${p.recorded}..${p.checkout}`]).catch(() => "")).split("\n").filter(Boolean);
        const title = subjects.length === 1 ? subjects[0] : `${subjects[0] || "engine"}${subjects.length > 1 ? ` (+${subjects.length - 1} more)` : ""}`;
        const pick = await vscode.window.showWarningMessage(
            `Commit the engine pointer ${p.recorded} -> ${p.checkout}?`,
            { modal: true, detail: [`One commit in the game, only engine/: "Update engine: ${title}".`, "", ...subjects.slice(0, 12).map((s) => `  ${s}`), "", "Nothing is pushed."].join("\n") },
            "Commit"
        );
        if (pick !== "Commit") return;
        try {
            await runGit(tool.cwd, ["commit", "-m", `Update engine: ${title}`, "--", "engine"]);
            ctx.addFeed("pointer", `engine pointer moved to ${p.checkout}: ${title}`);
        } catch (error) {
            vscode.window.showErrorMessage(`git commit engine: ${error.message}`);
        }
        ctx.refresh();
    }

    function openSessionTerminal(worker, verb) {
        const session = worker && worker.session;
        if (!session) return;
        const terminal = vscode.window.createTerminal({ name: `${worker.name} ${verb}`, cwd: worker.path, env: { PATH: toolEnv().PATH } });
        terminal.show();
        // By id: the name todo-w<k> is shared by every earlier session of the worker, and claude refuses it then.
        terminal.sendText(`${shellQuote(config().claude)} ${verb} ${shellQuote(session.id || session.name)}`);
    }

    async function stopWorker(worker) {
        if (!worker) return;
        const pick = await vscode.window.showWarningMessage(
            `Stop the session of ${worker.name}? Its entry stays "in progress" and the next start continues it.`,
            { modal: true },
            "Stop"
        );
        if (pick !== "Stop") return;
        try {
            const out = await runTool(findTool(), ["stop", String(worker.k), "--claude", config().claude], 60000);
            vscode.window.showInformationMessage(out.trim().split("\n")[0] || `${worker.name} stopped`);
        } catch (error) {
            vscode.window.showErrorMessage(`worker.py stop: ${error.message}`);
        }
        ctx.refresh();
    }

    // Update: worker.py update <k>, the loop's "go to the base" for an idle worker (every repository detached
    // at the tip of its base branch; the branch it held is kept). Refused for a running session or changes.
    async function updateWorker(worker) {
        const tool = findTool();
        if (!worker || !tool || busy.has(worker.k)) return;
        busy.add(worker.k);
        try {
            // A refusal exits 1 and its reason is the last line of stderr.
            const result = JSON.parse(await runTool(tool, ["update", String(worker.k), "--json", "--claude", config().claude], 60000).catch((error) => JSON.stringify({ updated: [], errors: [error.message.replace(/^error: /, "")] })));
            if (result.errors.length) {
                vscode.window.showErrorMessage(`Update of ${worker.name}: ${result.errors.join("; ")}`);
                return;
            }
            const done = result.updated[0];
            const text = done.repos.map((r) => `${path.basename(r.repo) === worker.name ? "game" : path.basename(r.repo)} at ${r.base} (${r.to})`).join(", ");
            if (done.notes.length) vscode.window.showWarningMessage(`${worker.name}: ${text}. ${done.notes.join("; ")}`);
            else vscode.window.showInformationMessage(`${worker.name}: ${text}`);
        } finally {
            busy.delete(worker.k);
            ctx.refresh();
        }
    }

    // Fast start: worker.py start <k> at once (no delay: one worker), /todo-loop with the slices, the loop
    // arguments and the report session of the settings. A worker whose session runs is left alone by worker.py.
    async function fastStart(worker) {
        const tool = findTool();
        if (!worker || !tool || busy.has(worker.k)) return;
        const c = config();
        const args = ["start", String(worker.k), "--delay", "0", "--slices", String(c.startSlices), "--claude", c.claude];
        if (c.loopArgs) args.push("--loop-args", c.loopArgs);
        if (c.reportTo) args.push("--report-to", c.reportTo);
        busy.add(worker.k);
        try {
            const out = await runTool(tool, args, 90000);
            const line = out.split("\n").map((l) => l.trim()).find((l) => l.startsWith("backgrounded") || l.includes("left alone")) || `${worker.name} started`;
            vscode.window.showInformationMessage(`${worker.name}: /todo-loop ${c.startSlices}: ${line}`);
        } catch (error) {
            vscode.window.showErrorMessage(`Fast start of ${worker.name}: ${error.message}`);
        } finally {
            busy.delete(worker.k);
            ctx.refresh();
        }
    }

    // Relaunch: worker.py relaunch, `claude --bg --resume` of the sessions the usage limit stopped, once it has reset
    // (the loop keeps its conversation). Without a worker every limited one; a named one also after another end.
    async function relaunch(worker) {
        const tool = findTool();
        if (!tool || (worker && busy.has(worker.k))) return;
        const c = config();
        const args = ["relaunch", ...(worker ? [String(worker.k)] : []), "--json", "--claude", c.claude];
        if (worker) busy.add(worker.k);
        try {
            const result = JSON.parse(await runTool(tool, args, 120000).catch((error) => JSON.stringify({ relaunched: [], skipped: [], errors: [error.message.replace(/^error: /, "")] })));
            const done = result.relaunched.map((r) => `${r.name}: ${r.output}`);
            if (result.errors.length) vscode.window.showErrorMessage(`Relaunch: ${[...done, ...result.errors].join("; ")}`);
            else if (done.length) vscode.window.showInformationMessage(`Relaunched ${done.join("; ")}`);
            else vscode.window.showInformationMessage("Relaunch: no worker was stopped by the usage limit");
        } finally {
            if (worker) busy.delete(worker.k);
            ctx.refresh();
        }
    }

    async function openPlan(entry) {
        const file = planFile(entry);
        if (!file || !fs.existsSync(file)) {
            if (entry && entry.planShown) return ctx.openTodoPlan(entry.planShown);
            vscode.window.showWarningMessage(`No local plan ${file || ""}: python3 engine/tools/todo/todo.py plan get todo-loop-${branchId(entry && entry.branch)}.md`);
            return;
        }
        vscode.window.showTextDocument(vscode.Uri.file(file), { preview: true });
    }

    // What the Todo list tab needs of the workers: who is free, who works which entry.
    function workersForList() {
        if (!ctx.state) return null;
        return ctx.state.workers.map((w) => ({ k: w.k, name: w.name, active: Boolean(w.active), dirty: Boolean(w.dirty), busy: busy.has(w.k), entry: w.entry ? w.entry.id : null, queue: w.queue || [] }));
    }

    // The ids of the entries whose branch lives in another checkout (worker.py list: elsewhere).
    function elsewhereForList() {
        return ctx.state && Array.isArray(ctx.state.elsewhere) ? ctx.state.elsewhere : [];
    }

    // A free worker: no session running, no uncommitted changes, no button of this panel at work on it.
    // The ones that hold no entry come first.
    function freeWorkers() {
        if (!ctx.state) return [];
        return ctx.state.workers
            .filter((w) => !w.active && !w.dirty && !busy.has(w.k))
            .sort((a, b) => (a.entry ? 1 : 0) - (b.entry ? 1 : 0) || a.k - b.k);
    }

    // The start dialog's modes: how far the loop goes from the entry. `slices` is the N of `/todo-loop N`.
    function startModes(through, startSlices) {
        const modes = [{ key: "one", label: "This entry only", description: "/todo-loop 1", slices: 1, note: "this entry only" }];
        if (startSlices > 1) modes.push({ key: "list", label: "This entry, then on through the list", description: `/todo-loop ${startSlices} (todoWorkers.startSlices)`, slices: startSlices, note: "then on through the list" });
        modes.push({
            key: "through",
            label: "Through to the end",
            description: through ? "/todo-loop 1; slicing is through already" : "sets slicing through (no cost cap), then /todo-loop 1",
            slices: 1,
            note: "through to the end",
            setThrough: !through,
        });
        return modes;
    }

    // Attach to free worker: first the mode (this entry only, then on through the list, through to the end), then
    // `/todo-loop N <id>` (with the loop arguments and the report session of the settings) as the background
    // session of a free worker, after the owner picks the worker. Each slice of a through entry restarts the worker
    // by itself (tools/todo/CLAUDE.md, "Workers"), so the third mode starts with `/todo-loop 1`.
    async function startEntryInWorker(id) {
        const tool = findTool();
        if (!tool || !id || !/^[A-Za-z0-9_-]+$/.test(id)) return;
        if (!ctx.state) await ctx.refresh();
        const state = ctx.state;
        const holder = state && state.workers.find((w) => w.active && w.entry && w.entry.id === id);
        if (holder) {
            vscode.window.showInformationMessage(`${id} is worked by ${holder.name} already.`);
            return;
        }
        if (elsewhereForList().includes(id)) {
            vscode.window.showWarningMessage(`${id}: its branch lives in another checkout or was cut from another base; continue it there.`);
            return;
        }
        // A worker that holds the entry's branch (an idle loop left it there) is the only one that can switch
        // to it: a branch is checked out in one worktree at a time.
        const owner = state && state.workers.find((w) => w.entry && w.entry.id === id);
        if (owner && (owner.dirty || busy.has(owner.k))) {
            vscode.window.showWarningMessage(`${owner.name} holds the branch of ${id} and ${owner.dirty ? "has uncommitted changes" : "is busy"}; it cannot start now.`);
            return;
        }
        const free = owner ? [owner] : freeWorkers();
        if (!free.length) {
            const why = state ? state.workers.map((w) => `${w.name}: ${w.active ? "session runs" : w.dirty ? "uncommitted changes" : busy.has(w.k) ? "busy" : "?"}`).join(", ") : "worker.py list did not answer";
            vscode.window.showWarningMessage(`No free worker for ${id} (${why}).`);
            return;
        }
        const c = config();
        let slicing;
        try {
            slicing = await ctx.entrySlicing(id);
        } catch (error) {
            vscode.window.showErrorMessage(`todo.py show ${id}: ${error.message}`);
            return;
        }
        const through = slicing === "through";
        const modePick = await vscode.window.showQuickPick(
            startModes(through, c.startSlices).map((m) => ({ label: m.label, description: m.description, mode: m })),
            { title: `${id} is ${through ? "already" : "not"} through`, placeHolder: "how far the loop goes from this entry" }
        );
        if (!modePick) return;
        const mode = modePick.mode;
        const loopArgs = [id, c.loopArgs].filter(Boolean).join(" ");
        const label = (w) => (w.entry && w.entry.id !== id ? `${w.name} (leaves ${w.entry.id})` : w.entry ? `${w.name} (holds its branch)` : w.name);
        const overlaps = await overlapWarning(id);
        const detail = ["It runs as the background session todo-w<k> of that worker, which worker.py start first moves to the tips of the bases. Attach opens it when it asks for a permission."];
        if (overlaps.length) detail.push("", "Busy entries name the same files (merging both later may conflict):", ...overlaps.map((o) => `  ${o}`));
        const picked = await vscode.window.showInformationMessage(`Start /todo-loop ${mode.slices} ${loopArgs} (${mode.note}) in a free worker?`, { modal: true, detail: detail.join("\n") }, ...free.map(label));
        const worker = free.find((w) => label(w) === picked);
        if (!worker || busy.has(worker.k)) return;
        const args = ["start", String(worker.k), "--delay", "0", "--slices", String(mode.slices), "--loop-args", loopArgs, "--claude", c.claude];
        if (c.reportTo) args.push("--report-to", c.reportTo);
        busy.add(worker.k);
        ctx.postWorkersToList();
        try {
            if (mode.setThrough) {
                try {
                    await ctx.setSlicingThrough(id);
                } catch (error) {
                    vscode.window.showErrorMessage(`${id}: slicing through was not set, nothing started: ${error.message}`);
                    return;
                }
            }
            const out = await runTool(tool, args, 90000);
            const line = out.split("\n").map((l) => l.trim()).find((l) => l.startsWith("backgrounded") || l.includes("left alone")) || `${worker.name} started`;
            vscode.window.showInformationMessage(`${worker.name} · ${id} (${mode.note}): ${line}`);
            ctx.addFeed("started", `${worker.name} started ${id} (${mode.note})`, { entry: id });
        } catch (error) {
            vscode.window.showErrorMessage(`${worker.name} · ${id}: ${error.message}`);
        } finally {
            busy.delete(worker.k);
            await ctx.refresh();
        }
    }

    Object.assign(ctx, {
        queueEntry, unqueueEntry, reorderQueue, autoStartQueues, commitPointer, openSessionTerminal, stopWorker, updateWorker, fastStart, relaunch,
        openPlan, workersForList, elsewhereForList, startEntryInWorker,
    });
}

module.exports = { attach };
