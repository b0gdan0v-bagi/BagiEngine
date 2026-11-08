// The panel of the parallel todo-loop workers (tools/todo/worker.py, "Workers" in tools/todo/CLAUDE.md).
// It reads `worker.py list --json --store --activity` and shows one card per worker (its entry, the state
// of its session, the plan, the branches, what the session and its agents do now) and the entries in
// review. It only reads and opens terminals; the loops are started by /todo-loop-parallel.
"use strict";

const vscode = require("vscode");
const childProcess = require("child_process");
const crypto = require("crypto");
const fs = require("fs");
const os = require("os");
const path = require("path");

const TOOL_PATHS = [path.join("engine", "tools", "todo", "worker.py"), path.join("tools", "todo", "worker.py")];

function config() {
    const c = vscode.workspace.getConfiguration("todoWorkers");
    return {
        python: c.get("python", "python3"),
        claude: c.get("claude", "claude"),
        refreshSeconds: Math.max(5, c.get("refreshSeconds", 15)),
        notify: c.get("notify", true),
    };
}

// VS Code started from the Dock does not see the PATH of a login shell, where `claude` usually is.
function toolEnv() {
    const home = os.homedir();
    const extra = [path.join(home, ".local", "bin"), path.join(home, ".claude", "local"), "/opt/homebrew/bin", "/usr/local/bin"];
    const parts = (process.env.PATH || "").split(path.delimiter).filter(Boolean);
    for (const dir of extra) {
        if (!parts.includes(dir)) parts.push(dir);
    }
    return { ...process.env, PATH: parts.join(path.delimiter) };
}

// The main checkout and its worker.py: the first workspace folder that has one.
function findTool() {
    for (const folder of vscode.workspace.workspaceFolders || []) {
        for (const rel of TOOL_PATHS) {
            const script = path.join(folder.uri.fsPath, rel);
            if (fs.existsSync(script)) return { cwd: folder.uri.fsPath, script };
        }
    }
    return null;
}

function runTool(tool, args, timeout) {
    const { python } = config();
    return new Promise((resolve, reject) => {
        childProcess.execFile(python, [tool.script, ...args], { cwd: tool.cwd, env: toolEnv(), timeout, maxBuffer: 16 * 1024 * 1024 }, (error, stdout, stderr) => {
            if (error) {
                const text = (stderr || stdout || error.message).trim().split("\n").pop();
                reject(new Error(text || error.message));
            } else {
                resolve(stdout);
            }
        });
    });
}

function branchId(branch) {
    return branch ? branch.replace(/^todo\//, "") : "";
}

function planFile(entry) {
    return entry && entry.branch ? path.join(os.homedir(), ".claude", "plans", `todo-loop-${branchId(entry.branch)}.md`) : null;
}

function shellQuote(text) {
    return /^[A-Za-z0-9._\/-]+$/.test(text) ? text : `'${text.replace(/'/g, "'\\''")}'`;
}

function sessionWord(worker) {
    const s = worker.session;
    if (!s) return "no session";
    if (s.state === "done") return "done";
    return s.status === "busy" ? "busy" : "idle";
}

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
        view.webview.html = this.html(view.webview);
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

    html(webview) {
        const nonce = crypto.randomBytes(16).toString("hex");
        const script = fs.readFileSync(path.join(this.context.extensionPath, "media", "panel.js"), "utf8");
        const style = fs.readFileSync(path.join(this.context.extensionPath, "media", "panel.css"), "utf8");
        return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'nonce-${nonce}'; script-src 'nonce-${nonce}';">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style nonce="${nonce}">${style}</style>
</head>
<body>
<div id="root"><p class="muted">Asking worker.py ...</p></div>
<script nonce="${nonce}">${script}</script>
</body>
</html>`;
    }
}

function activate(context) {
    const status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
    status.command = "workbench.view.extension.todoWorkers";
    context.subscriptions.push(status);

    let state = null; // the last answer of worker.py
    let running = false;
    let timer = null;
    let previous = null; // {review: Set, active: Map}

    const workerOf = (k) => state && state.workers.find((w) => w.k === k);
    const entryOf = (id) => state && [...state.review, ...state.workers.map((w) => w.entry)].find((e) => e && e.id === id);

    function openSessionTerminal(worker, verb) {
        const session = worker && worker.session;
        if (!session) return;
        const terminal = vscode.window.createTerminal({ name: `${worker.name} ${verb}`, cwd: worker.path, env: { PATH: toolEnv().PATH } });
        terminal.show();
        terminal.sendText(`${shellQuote(config().claude)} ${verb} ${shellQuote(session.name || session.id)}`);
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
        refresh();
    }

    async function openPlan(entry) {
        const file = planFile(entry);
        if (!file || !fs.existsSync(file)) {
            vscode.window.showWarningMessage(`No local plan ${file || ""}: python3 engine/tools/todo/todo.py plan get todo-loop-${branchId(entry && entry.branch)}.md`);
            return;
        }
        vscode.window.showTextDocument(vscode.Uri.file(file), { preview: true });
    }

    async function copyMerge(entry) {
        if (!entry || !entry.branch) return;
        await vscode.env.clipboard.writeText(`/todo-merge ${entry.branch}`);
        vscode.window.showInformationMessage(`Copied: /todo-merge ${entry.branch} (paste it into Claude in the main checkout)`);
    }

    function act(message) {
        const worker = workerOf(message.worker);
        switch (message.cmd) {
            case "refresh": return refresh();
            case "attach": return openSessionTerminal(worker, "attach");
            case "logs": return openSessionTerminal(worker, "logs");
            case "folder": return worker && vscode.commands.executeCommand("vscode.openFolder", vscode.Uri.file(worker.path), { forceNewWindow: true });
            case "terminal": return worker && vscode.window.createTerminal({ name: worker.name, cwd: worker.path }).show();
            case "stop": return stopWorker(worker);
            case "plan": return openPlan(entryOf(message.entry));
            case "merge": return copyMerge(entryOf(message.entry));
            case "settings": return vscode.commands.executeCommand("workbench.action.openSettings", "todoWorkers");
        }
    }

    const view = new WorkersView(context, act);
    context.subscriptions.push(vscode.window.registerWebviewViewProvider("todoWorkers.view", view, { webviewOptions: { retainContextWhenHidden: true } }));

    function notifyChanges() {
        const now = { review: new Set(state.review.map((e) => e.id)), active: new Map(state.workers.map((w) => [w.name, w.active])) };
        if (previous && config().notify) {
            for (const entry of state.review) {
                if (!previous.review.has(entry.id)) {
                    vscode.window.showInformationMessage(`Todo: ${entry.id} is in review (${entry.branch})`, "Copy /todo-merge").then((pick) => pick && copyMerge(entry));
                }
            }
            for (const worker of state.workers) {
                if (previous.active.get(worker.name) && !worker.active) {
                    vscode.window.showInformationMessage(`Todo: the session of ${worker.name} has ended`, "Show log").then((pick) => pick && openSessionTerminal(worker, "logs"));
                }
            }
        }
        if (state.store === "ok" && state.sessionsKnown) previous = now;
    }

    function updateStatus(error) {
        if (!state) {
            status.text = "$(server-process) todo ?";
            status.tooltip = error || "Todo Workers";
            status.backgroundColor = undefined;
            status.show();
            return;
        }
        if (!state.workers.length) {
            status.hide();
            return;
        }
        const count = (word) => state.workers.filter((w) => sessionWord(w) === word).length;
        const parts = [`${count("busy")} busy`];
        if (count("idle")) parts.push(`${count("idle")} idle`);
        parts.push(`${state.review.length} review`);
        status.text = `$(server-process) todo ${parts.join(" · ")}`;
        status.tooltip = state.workers.map((w) => `${w.name}: ${w.entry ? w.entry.id : "no entry"} (${sessionWord(w)})`).join("\n");
        status.backgroundColor = state.review.length ? new vscode.ThemeColor("statusBarItem.warningBackground") : undefined;
        status.show();
    }

    async function refresh() {
        if (running) return;
        const tool = findTool();
        if (!tool) {
            state = null;
            view.show({ error: "No tools/todo/worker.py in this workspace" });
            status.hide();
            return;
        }
        running = true;
        view.show({ ...(state ? { state } : { loading: true }), refreshing: true });
        try {
            const out = await runTool(tool, ["list", "--json", "--store", "--activity", "--claude", config().claude], 90000);
            state = JSON.parse(out);
            view.show({ state, at: Date.now() });
            updateStatus(null);
            notifyChanges();
        } catch (error) {
            view.show({ ...(state ? { state } : {}), error: `worker.py list: ${error.message}`, at: Date.now() });
            if (!state) updateStatus(error.message);
        } finally {
            running = false;
        }
    }

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
    refresh();
}

function deactivate() {}

module.exports = { activate, deactivate };
