// The Todo list tab: the entries of the store (todo.py list --all --json --body), read again on Refresh and on every
// write signal, and the owner's writes from it: a new entry, status, stage, priority, body, through and its cost cap,
// dependencies, the order of a worker's queue. Every write is todo.py with the actor `owner-panel`.
"use strict";

const vscode = require("vscode");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { findTool, todoTool, runTool, pageHtml } = require("./tools");

const ACTOR = "owner-panel";
const ID_RE = /^[A-Za-z0-9_-]+$/;
const DRAFTS = path.join(os.homedir(), ".claude", "plans"); // outside every work tree, like the plans
const STORE_BUTTON = "Store it";

function attach(ctx) {
    let todoPanel = null;
    let todoLoading = false;
    let todoAgain = false;
    let entries = []; // the last list, for the pickers and the duplicate hint

    function todo(args, input) {
        const tool = findTool();
        if (!tool) return Promise.reject(new Error("no tools/todo/todo.py in the workspace"));
        return runTool(todoTool(tool), ["--actor", ACTOR, ...args], 30000, input);
    }

    async function loadTodoList() {
        const tool = findTool();
        if (!todoPanel) return;
        if (todoLoading) {
            todoAgain = true;
            return;
        }
        if (!tool) {
            todoPanel.webview.postMessage({ error: "No tools/todo/todo.py in the workspace folders." });
            return;
        }
        todoLoading = true;
        todoPanel.webview.postMessage({ loading: true, error: null });
        try {
            entries = JSON.parse(await runTool(todoTool(tool), ["list", "--all", "--json", "--body"], 30000));
            const state = ctx.state;
            if (todoPanel) todoPanel.webview.postMessage({ loading: false, entries, areas: state && state.areas ? state.areas : null, workers: ctx.workersForList(), elsewhere: ctx.elsewhereForList(), at: Date.now() });
        } catch (error) {
            if (todoPanel) todoPanel.webview.postMessage({ loading: false, error: `todo.py list: ${error.message}` });
        } finally {
            todoLoading = false;
            if (todoAgain) {
                todoAgain = false;
                loadTodoList();
            }
        }
    }

    function postWorkersToList() {
        if (todoPanel) todoPanel.webview.postMessage({ workers: ctx.workersForList(), elsewhere: ctx.elsewhereForList() });
    }

    async function showTodoEntry(id) {
        const tool = findTool();
        if (!tool || !id) return;
        try {
            const text = await runTool(todoTool(tool), ["show", id], 30000);
            const doc = await vscode.workspace.openTextDocument({ language: "markdown", content: text });
            try {
                await vscode.commands.executeCommand("markdown.showPreview", doc.uri);
            } catch (error) {
                await vscode.window.showTextDocument(doc, { preview: true });
            }
        } catch (error) {
            vscode.window.showErrorMessage(`todo.py show ${id}: ${error.message}`);
        }
    }

    // A plan by its name: the local copy in ~/.claude/plans, else the text from the store.
    async function openTodoPlan(name) {
        const tool = findTool();
        if (!tool || !name || !/^[A-Za-z0-9._-]+$/.test(name)) return;
        const file = path.join(DRAFTS, name);
        if (fs.existsSync(file)) {
            await vscode.window.showTextDocument(vscode.Uri.file(file), { preview: true });
            return;
        }
        try {
            const text = await runTool(todoTool(tool), ["plan", "get", name, "--stdout"], 30000);
            const doc = await vscode.workspace.openTextDocument({ language: "markdown", content: text });
            await vscode.window.showTextDocument(doc, { preview: true });
        } catch (error) {
            vscode.window.showErrorMessage(`todo.py plan get ${name}: ${error.message}`);
        }
    }

    // --- writes ---------------------------------------------------------------------------------------------

    async function write(what, args, input) {
        try {
            const out = await todo(args, input);
            vscode.window.showInformationMessage(out.trim().split("\n")[0] || what);
        } catch (error) {
            vscode.window.showErrorMessage(`${what}: ${error.message}`);
        }
        // the store's write signal reads the list again; this covers a missing watcher
        loadTodoList();
    }

    // A text edited in the editor: a draft file outside git, opened, then stored on the owner's word.
    async function editDraft(name, initial, prompt) {
        fs.mkdirSync(DRAFTS, { recursive: true });
        const file = path.join(DRAFTS, name);
        fs.writeFileSync(file, initial);
        await vscode.window.showTextDocument(vscode.Uri.file(file), { preview: false });
        const pick = await vscode.window.showInformationMessage(prompt, STORE_BUTTON, "Cancel");
        if (pick !== STORE_BUTTON) return null;
        const doc = vscode.workspace.textDocuments.find((d) => d.uri.fsPath === file);
        if (doc && doc.isDirty) await doc.save();
        const text = fs.readFileSync(file, "utf8").replace(/<!--[\s\S]*?-->\n?/g, "").trim();
        fs.unlinkSync(file);
        return text;
    }

    function words(text) {
        return String(text || "").toLowerCase().split(/[^a-z0-9]+/).filter((w) => w.length > 3);
    }

    async function newEntry() {
        const state = ctx.state;
        const areas = (state && state.areas) || [...new Set(entries.map((e) => e.area))].sort();
        const area = areas.length === 1 ? areas[0] : await vscode.window.showQuickPick(areas, { placeHolder: "the area of the new entry" });
        if (!area) return;
        const title = await vscode.window.showInputBox({ prompt: `Title of the new ${area} entry (English, what is missing or wrong)`, ignoreFocusOut: true });
        if (!title) return;
        const own = new Set(words(title));
        const similar = entries
            .map((e) => ({ e, hits: words(`${e.id} ${e.title}`).filter((w) => own.has(w)).length }))
            .filter((x) => x.hits >= 2)
            .sort((a, b) => b.hits - a.hits)
            .slice(0, 5);
        if (similar.length) {
            const go = await vscode.window.showWarningMessage(
                "Entries with a similar title exist: add to one of them instead?",
                { modal: true, detail: similar.map((x) => `${x.e.id} (${x.e.status}): ${x.e.title}`).join("\n") },
                "Add a new one anyway"
            );
            if (!go) return;
        }
        const guess = title.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "").slice(0, 40).replace(/-$/, "");
        const id = await vscode.window.showInputBox({ prompt: "Id (kebab-case, unique over every area; branches are named by it)", value: guess, ignoreFocusOut: true, validateInput: (v) => (ID_RE.test(v) ? null : "letters, digits, '-' and '_' only") });
        if (!id) return;
        const body = await editDraft(
            `new-entry-${id}.md`,
            "<!-- The body of the new entry (no title line). **What is missing:** is required: the dashboard takes the summary from it. -->\n**Where:** \n\n**What is missing:** \n\n**A fix has to cover:**\n- \n",
            `New entry ${id}: write its text in the open file, save, then store it.`
        );
        if (body === null) return;
        await write(`todo.py add ${id}`, ["add", id, "--area", area, "--title", title, "--body-file", "-"], body);
    }

    async function setStatus(id, status) {
        if (!ID_RE.test(id) || !["open", "deferred"].includes(status)) return;
        await write(`todo.py edit ${id}`, ["edit", id, "--status", status]);
    }

    async function setStage(id, area) {
        let stages = [];
        try {
            const areas = JSON.parse(await todo(["area", "list", "--json"]));
            stages = ((areas.find((a) => a.id === area) || {}).stages || []).map((s) => ({ label: `stage ${s.number}`, description: s.title, value: String(s.number) }));
        } catch (error) {
            vscode.window.showErrorMessage(`todo.py area list: ${error.message}`);
            return;
        }
        const pick = await vscode.window.showQuickPick([...stages, { label: "none", description: "no stage", value: "none" }], { placeHolder: `the stage of ${id}` });
        if (pick) await write(`todo.py edit ${id}`, ["edit", id, "--stage", pick.value]);
    }

    async function setPriority(id) {
        const current = (entries.find((e) => e.id === id) || {}).priority || "none";
        const levels = [
            { label: "now", description: "the loop takes it first" },
            { label: "high" },
            { label: "normal", description: "the same as none for the order" },
            { label: "low", description: "after every other entry" },
            { label: "none", description: "no priority set" },
        ].map((l) => ({ ...l, description: [l.description, l.label === current ? "current" : ""].filter(Boolean).join(" · ") }));
        const pick = await vscode.window.showQuickPick(levels, { placeHolder: `the priority of ${id} (now ${current})` });
        if (pick && pick.label !== current) await write(`todo.py edit ${id}`, ["edit", id, "--priority", pick.label]);
    }

    async function editBody(id) {
        let entry;
        try {
            entry = JSON.parse(await todo(["show", id, "--json"]));
        } catch (error) {
            vscode.window.showErrorMessage(`todo.py show ${id}: ${error.message}`);
            return;
        }
        const body = await editDraft(`edit-entry-${id}.md`, `<!-- The body of ${id} at version ${entry.version}; a change made meanwhile refuses this store. -->\n${entry.body}`, `Edit the text of ${id} in the open file, save, then store it.`);
        if (body === null) return;
        await write(`todo.py edit ${id}`, ["edit", id, "--body-file", "-", "--expected-version", String(entry.version)], body + "\n");
    }

    async function setThrough(id, on) {
        if (!on) return write(`todo.py edit ${id}`, ["edit", id, "--slicing", "none"]);
        const cap = await vscode.window.showInputBox({
            prompt: `Through for ${id}: a cost cap in dollars (API prices over every loop session of the entry), empty for none`,
            ignoreFocusOut: true,
            validateInput: (v) => (!v.trim() || (Number(v) > 0 && Number.isFinite(Number(v))) ? null : "a positive number, or empty"),
        });
        if (cap === undefined) return;
        await write(`todo.py edit ${id}`, ["edit", id, "--slicing", "through", "--cost-cap", cap.trim() ? String(Number(cap)) : "none"]);
    }

    // The slicing of an entry for the start dialog: from the list held, else from the store.
    async function entrySlicing(id) {
        const held = entries.find((e) => e.id === id);
        if (held && held.slicing !== undefined) return held.slicing;
        return JSON.parse(await todo(["show", id, "--json"])).slicing || null;
    }

    // Through without a cost cap question (the start dialog's third mode); the caller handles the error.
    async function setSlicingThrough(id) {
        await todo(["edit", id, "--slicing", "through"]);
        loadTodoList();
    }

    async function depAdd(id) {
        const candidates = entries.filter((e) => e.id !== id && e.status !== "closed").map((e) => ({ label: e.id, description: `${e.area} · ${e.status}`, detail: e.title }));
        const pick = await vscode.window.showQuickPick(candidates, { placeHolder: `${id} waits for ...`, matchOnDetail: true });
        if (!pick) return;
        const note = await vscode.window.showInputBox({ prompt: `Why ${id} waits for ${pick.label} (a few words, optional)`, ignoreFocusOut: true });
        if (note === undefined) return;
        await write(`todo.py dep add ${id}`, ["dep", "add", id, pick.label, ...(note.trim() ? ["--note", note.trim()] : [])]);
    }

    async function depRm(id, blocker) {
        if (!ID_RE.test(id) || !ID_RE.test(blocker)) return;
        const pick = await vscode.window.showWarningMessage(`${id} no longer waits for ${blocker}?`, { modal: true }, "Remove the link");
        if (pick) await write(`todo.py dep rm ${id}`, ["dep", "rm", id, blocker]);
    }

    function openTodoList() {
        if (todoPanel) {
            todoPanel.reveal();
            return loadTodoList();
        }
        todoPanel = vscode.window.createWebviewPanel("todoWorkers.todoList", "Todo list", vscode.ViewColumn.Active, { enableScripts: true, retainContextWhenHidden: true });
        todoPanel.iconPath = vscode.Uri.file(path.join(ctx.context.extensionPath, "media", "workers.svg"));
        todoPanel.webview.html = pageHtml(ctx.context, "todo.js", ["panel.css", "todo.css"], "Asking todo.py ...");
        todoPanel.webview.onDidReceiveMessage((message) => {
            switch (message.cmd) {
                case "ready":
                case "refresh": return loadTodoList();
                case "show": return showTodoEntry(message.entry);
                case "plan": return openTodoPlan(message.name);
                case "copy": return vscode.env.clipboard.writeText(String(message.text || ""));
                case "startInWorker": return ctx.startEntryInWorker(message.entry);
                case "queue": return ctx.queueEntry(message.entry);
                case "unqueue": return ctx.unqueueEntry(message.entry, message.worker);
                case "reorderQueue": return ctx.reorderQueue(message.worker, message.ids);
                case "newEntry": return newEntry();
                case "setStatus": return setStatus(message.entry, message.status);
                case "setStage": return setStage(message.entry, message.area);
                case "setPriority": return setPriority(message.entry);
                case "editBody": return editBody(message.entry);
                case "setThrough": return setThrough(message.entry, Boolean(message.on));
                case "depAdd": return depAdd(message.entry);
                case "depRm": return depRm(message.entry, message.blocker);
            }
        });
        todoPanel.onDidChangeViewState((event) => {
            if (event.webviewPanel.visible) loadTodoList();
        });
        todoPanel.onDidDispose(() => {
            todoPanel = null;
        });
    }

    Object.assign(ctx, { openTodoList, loadTodoList, postWorkersToList, openTodoPlan, setThrough, entrySlicing, setSlicingThrough });
}

module.exports = { attach };
