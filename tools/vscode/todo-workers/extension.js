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
//   src/insights.js  the slower views (busy files, the day, the timeline, review rounds), the doctor's fixes, the merge
//                    train, the end of the day, Dispatch, the push to a phone
// Both views read again at once when todo.py signals a write (~/.claude/todo/changed.json), besides the timer.
"use strict";

const vscode = require("vscode");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { config, findTool, runTool, pageHtml } = require("./src/tools");

const STORE_CHANGE_DEBOUNCE_MS = 700;
const STICKY_KEYS = ["insights", "machine", "machineHistory", "feed", "lastVerify"]; // a claim and its stub plan are two writes: one refresh

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
        view.webview.html = pageHtml(this.context, ["graph.js", "insights.js", "panel.js"], ["panel.css", "graph.css"], "Asking worker.py ...");
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

    // What each module reads on its own timer (the views of insights.js, the machine and its history, the feed, the
    // last verify) stays until that module sends a newer one: a show from another module never drops it.
    show(payload) {
        const kept = {};
        for (const key of STICKY_KEYS) if (this.payload[key] !== undefined) kept[key] = this.payload[key];
        this.payload = { ...kept, ...payload };
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

    for (const name of ["feed", "build", "workers", "review", "todoList", "insights"]) require(`./src/${name}`).attach(ctx);

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
            case "reorderQueue": return ctx.reorderQueue(message.worker, message.ids);
            case "commitPointer": return ctx.commitPointer();
            case "clearFeed": return ctx.clearFeed();
            case "review": return ctx.reviewBranch(entryOf(message.entry));
            case "claudeReview": return ctx.startClaudeReview(entryOf(message.entry));
            case "todoList": return ctx.openTodoList(message.entry);
            case "doctorFix": return ctx.doctorFix(message.worker, message.kind);
            case "mergeTrain": return ctx.mergeTrain();
            case "endDay": return ctx.endDay();
            case "copyPush": return ctx.copyPush();
            case "dispatch": return ctx.dispatch();
            case "insights": return ctx.readInsights("now");
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
        // keep what is shown (the feed, the insights, the last verify) while worker.py answers
        view.show(ctx.state ? { ...view.payload, refreshing: true } : { loading: true, refreshing: true, insights: ctx.insights(), ...ctx.machine() });
        try {
            // hidden, the panel only needs what the status bar and the notifications show: no transcripts read for
            // the last steps and the tokens (the usage ledger catches up on the next full read: its counts are totals)
            const full = view.visible ? ["--activity", "--usage"] : [];
            ctx.readMachine(); // beside the list: cheap, and the load history needs it hidden too
            const out = await runTool(tool, ["list", "--json", "--store", ...full, "--claude", config().claude], 90000);
            ctx.state = JSON.parse(out);
            // A branch of another checkout or machine (or cut from another base) is not this checkout's to review or
            // merge: it leaves the review list, the status bar count and the "reached review" notices for a folded line.
            const away = (e) => e.local === false || Boolean(e.foreignBase);
            ctx.state.reviewElsewhere = (ctx.state.review || []).filter(away);
            ctx.state.review = (ctx.state.review || []).filter((e) => !away(e));
            const info = ctx.cachedBuildInfo() || (await ctx.getBuildInfo());
            view.show({ state: ctx.state, at: Date.now(), feed: ctx.feed(), lastVerify: ctx.lastVerify(), prices: config().prices, shotsTool: Boolean(info && info.shots), verifying: ctx.isVerifying() ? "running" : null, insights: ctx.insights(), ...ctx.machine() });
            ctx.postWorkersToList();
            ctx.updateStatus(null);
            ctx.notifyChanges();
            ctx.autoStartQueues();
            if (view.visible) ctx.readInsights("timer"); // each view on its own slower interval
        } catch (error) {
            view.show({ ...view.payload, refreshing: false, error: `worker.py list: ${error.message}`, at: Date.now() });
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
                    if (view.visible) ctx.readInsights("write");
                }, STORE_CHANGE_DEBOUNCE_MS);
            });
            watcher.on("error", () => watcher.close());
            context.subscriptions.push({ dispose: () => { clearTimeout(pending); watcher.close(); } });
        } catch (error) {
            // no hook (no folder, no watcher): the timer still refreshes
        }
    }

    // Visible, the panel reads every refreshSeconds; hidden (only the status bar and the notifications need it) every
    // hiddenRefreshSeconds. A write to the store reads at once either way (watchStoreChanges).
    let timer = null;
    let lastTick = 0;
    function schedule() {
        if (timer) clearInterval(timer);
        timer = setInterval(() => {
            const every = (view.visible ? config().refreshSeconds : config().hiddenRefreshSeconds) * 1000;
            if (Date.now() - lastTick < every - 500) return;
            lastTick = Date.now();
            refresh();
        }, 5000);
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
