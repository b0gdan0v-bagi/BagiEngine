// The panel of the parallel todo-loop workers (tools/todo/worker.py, "Workers" in tools/todo/CLAUDE.md).
// It reads `worker.py list --json --store --activity --usage` and shows one card per worker and the entries in
// review; the Todo list tab shows the store and takes the owner's edits. This file wires the modules of src/ around
// one shared `ctx` (the last state, the view, the status bar, the workers a button is busy with); each module adds
// its actions to `ctx`, so they call each other through it:
//   src/tools.js     settings, worker.py / todo.py / git, PATH, the webview page shell
//   src/feed.js      the feed, the notifications of changes, the status bar
//   src/build.js     Verify, Build & run, Shots
//   src/workers.js   sessions, update, fast start, relaunch, queues, start in a free worker, the engine pointer
//   src/review.js    review, Review with Claude, Quick merge, Merge with Claude, Return with remarks
//   src/todoList.js  the Todo list tab and the owner's writes to the store
// Both views read again at once when todo.py signals a write (~/.claude/todo/changed.json), besides the timer.
"use strict";

const vscode = require("vscode");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { config, findTool, runTool, pageHtml } = require("./src/tools");

const STORE_CHANGE_DEBOUNCE_MS = 700; // a claim and its stub plan are two writes: one refresh

class WorkersView {
    constructor(context, actions) {
        this.context = context;
        this.actions = actions;
        this.view = null;
        this.payload = { loading: true };
    }

    resolveWebviewView(view) {
        this.view = view;
        view.webview.options = { enableScripts: true };
        view.webview.html = pageHtml(this.context, "panel.js", ["panel.css"], "Asking worker.py ...");
        view.webview.onDidReceiveMessage((message) => this.actions(message));
        view.onDidChangeVisibility(() => {
            if (view.visible) this.actions({ cmd: "refresh" });
        });
        view.onDidDispose(() => {
            this.view = null;
        });
        this.post();
    }

    get visible() {
        return Boolean(this.view && this.view.visible);
    }

    show(payload) {
        this.payload = payload;
        this.post();
    }

    post() {
        if (this.view) this.view.webview.postMessage(this.payload);
    }
}

function activate(context) {
    const status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
    status.command = "workbench.view.extension.todoWorkers";
    context.subscriptions.push(status);

    const ctx = { context, status, state: null, busy: new Set() };
    ctx.workerOf = (k) => ctx.state && ctx.state.workers.find((w) => w.k === k);
    const entryOf = (id) => ctx.state && [...ctx.state.review, ...ctx.state.workers.map((w) => w.entry)].find((e) => e && e.id === id);

    for (const name of ["feed", "build", "workers", "review", "todoList"]) require(`./src/${name}`).attach(ctx);

    function act(message) {
        const worker = ctx.workerOf(message.worker);
        switch (message.cmd) {
            case "refresh": return refresh();
            case "attach": return ctx.openSessionTerminal(worker, "attach");
            case "logs": return ctx.openSessionTerminal(worker, "logs");
            case "folder": return worker && vscode.commands.executeCommand("vscode.openFolder", vscode.Uri.file(worker.path), { forceNewWindow: true });
            case "terminal": return worker && vscode.window.createTerminal({ name: worker.name, cwd: worker.path }).show();
            case "stop": return ctx.stopWorker(worker);
            case "update": return ctx.updateWorker(worker);
            case "fastStart": return ctx.fastStart(worker);
            case "relaunch": return ctx.relaunch(worker);
            case "relaunchAll": return ctx.relaunch(null);
            case "plan": return ctx.openPlan(entryOf(message.entry));
            case "merge": return ctx.startMerge(entryOf(message.entry));
            case "quickMerge": return ctx.quickMerge(entryOf(message.entry), false);
            case "mergeTest": return ctx.quickMerge(entryOf(message.entry), true);
            case "remarks": return ctx.returnWithRemarks(entryOf(message.entry));
            case "through": return ctx.setThrough(message.entry, Boolean(message.on)).then(refresh);
            case "verify": return ctx.verifyMain(null);
            case "run": return message.entry ? ctx.runBranch(entryOf(message.entry), null) : ctx.runBranch(worker && worker.entry, worker);
            case "shots": return ctx.shots(entryOf(message.entry));
            case "unqueue": return ctx.unqueueEntry(message.entry, message.worker);
            case "commitPointer": return ctx.commitPointer();
            case "clearFeed": return ctx.clearFeed();
            case "review": return ctx.reviewBranch(entryOf(message.entry));
            case "claudeReview": return ctx.startClaudeReview(entryOf(message.entry));
            case "todoList": return ctx.openTodoList();
            case "settings": return vscode.commands.executeCommand("workbench.action.openSettings", "todoWorkers");
        }
    }

    const view = new WorkersView(context, act);
    ctx.view = view;
    context.subscriptions.push(vscode.window.registerWebviewViewProvider("todoWorkers.view", view, { webviewOptions: { retainContextWhenHidden: true } }));

    let running = false;
    let refreshAgain = false;
    async function refresh() {
        if (running) {
            refreshAgain = true;
            return;
        }
        const tool = findTool();
        if (!tool) {
            ctx.state = null;
            view.show({ error: "No tools/todo/worker.py in this workspace" });
            status.hide();
            return;
        }
        running = true;
        view.show({ ...(ctx.state ? { state: ctx.state } : { loading: true }), refreshing: true });
        try {
            const out = await runTool(tool, ["list", "--json", "--store", "--activity", "--usage", "--claude", config().claude], 90000);
            ctx.state = JSON.parse(out);
            const info = ctx.cachedBuildInfo() || (await ctx.getBuildInfo());
            view.show({ state: ctx.state, at: Date.now(), feed: ctx.feed(), lastVerify: ctx.lastVerify(), prices: config().prices, shotsTool: Boolean(info && info.shots), verifying: ctx.isVerifying() ? "running" : null });
            ctx.postWorkersToList();
            ctx.updateStatus(null);
            ctx.notifyChanges();
            ctx.autoStartQueues();
        } catch (error) {
            view.show({ ...(ctx.state ? { state: ctx.state } : {}), error: `worker.py list: ${error.message}`, at: Date.now() });
            if (!ctx.state) ctx.updateStatus(error.message);
        } finally {
            running = false;
            if (refreshAgain) {
                refreshAgain = false;
                refresh();
            }
        }
    }
    ctx.refresh = refresh;

    // The hook: todo.py rewrites ~/.claude/todo/changed.json (TODO_CHANGED_FILE) after every write the store
    // accepts (a claim, a review, a close, a plan put, ...), from a worker's loop, a merge or the owner's CLI.
    // A change reads the workers and the Todo list tab again at once; the timer stays for what moves without a
    // write (the sessions, their last steps and tokens) and for edits a person makes in Vikunja.
    function watchStoreChanges() {
        const file = process.env.TODO_CHANGED_FILE || path.join(os.homedir(), ".claude", "todo", "changed.json");
        const dir = path.dirname(file);
        const name = path.basename(file);
        let pending = null;
        try {
            fs.mkdirSync(dir, { recursive: true });
            const watcher = fs.watch(dir, (event, filename) => {
                if (filename && filename !== name) return; // the temporary file of the rename
                clearTimeout(pending);
                pending = setTimeout(() => {
                    refresh();
                    ctx.loadTodoList();
                }, STORE_CHANGE_DEBOUNCE_MS);
            });
            watcher.on("error", () => watcher.close());
            context.subscriptions.push({ dispose: () => { clearTimeout(pending); watcher.close(); } });
        } catch (error) {
            // no hook (no folder, no watcher): the timer still refreshes
        }
    }

    let timer = null;
    function schedule() {
        if (timer) clearInterval(timer);
        timer = setInterval(() => {
            if (view.visible || status.visible) refresh();
        }, config().refreshSeconds * 1000);
    }

    context.subscriptions.push(
        vscode.commands.registerCommand("todoWorkers.refresh", refresh),
        vscode.workspace.onDidChangeConfiguration((event) => {
            if (event.affectsConfiguration("todoWorkers")) {
                schedule();
                refresh();
            }
        }),
        vscode.workspace.onDidChangeWorkspaceFolders(refresh),
        { dispose: () => timer && clearInterval(timer) }
    );
    schedule();
    watchStoreChanges();
    refresh();
}

function deactivate() {}

module.exports = { activate, deactivate };
