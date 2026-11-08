// The panel of the parallel todo-loop workers (tools/todo/worker.py, "Workers" in tools/todo/CLAUDE.md).
// It only reads `worker.py list --json --store` and starts terminals; the loops themselves run in
// background Claude sessions started by /todo-loop-parallel.
"use strict";

const vscode = require("vscode");
const childProcess = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");

const TOOL_PATHS = [path.join("engine", "tools", "todo", "worker.py"), path.join("tools", "todo", "worker.py")];

function config() {
    const c = vscode.workspace.getConfiguration("todoWorkers");
    return {
        python: c.get("python", "python3"),
        claude: c.get("claude", "claude"),
        refreshSeconds: Math.max(5, c.get("refreshSeconds", 20)),
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
        childProcess.execFile(python, [tool.script, ...args], { cwd: tool.cwd, env: toolEnv(), timeout, maxBuffer: 8 * 1024 * 1024 }, (error, stdout, stderr) => {
            if (error) {
                const text = (stderr || stdout || error.message).trim().split("\n").pop();
                reject(new Error(text || error.message));
            } else {
                resolve(stdout);
            }
        });
    });
}

function age(startedAt) {
    if (typeof startedAt !== "number") return "";
    const minutes = Math.max(0, Math.floor((Date.now() - startedAt) / 60000));
    return minutes >= 60 ? `${Math.floor(minutes / 60)} h ${minutes % 60} min` : `${minutes} min`;
}

function headText(head) {
    if (!head) return "";
    return head.branch || `detached ${head.commit}`;
}

function sessionText(worker) {
    const s = worker.session;
    if (!s) return "no session";
    const state = s.state || s.status || "?";
    const shown = state === s.status || !s.status ? state : `${state}/${s.status}`;
    const since = age(s.startedAt);
    return since ? `${shown}, ${since}` : shown;
}

function progressText(entry) {
    const p = entry && entry.planProgress;
    return p && p.total ? `${p.done}/${p.total}` : "";
}

function branchId(branch) {
    return branch ? branch.replace(/^todo\//, "") : "";
}

function planFile(entry) {
    return entry && entry.branch ? path.join(os.homedir(), ".claude", "plans", `todo-loop-${branchId(entry.branch)}.md`) : null;
}

function workerIcon(worker) {
    const s = worker.session;
    if (!s) return new vscode.ThemeIcon("circle-outline");
    if (s.state === "done") return new vscode.ThemeIcon("pass", new vscode.ThemeColor("testing.iconPassed"));
    if (s.status === "busy") return new vscode.ThemeIcon("sync~spin");
    // idle but not done: it waits for an agent of its own, or for the person (attach to see)
    return new vscode.ThemeIcon("watch", new vscode.ThemeColor("list.warningForeground"));
}

class Item extends vscode.TreeItem {
    constructor(label, collapsible, data) {
        super(label, collapsible);
        Object.assign(this, data || {});
    }
}

class WorkersProvider {
    constructor() {
        this.changed = new vscode.EventEmitter();
        this.onDidChangeTreeData = this.changed.event;
        this.state = null; // the last answer of worker.py
        this.error = null;
        this.loading = true;
    }

    set(state, error) {
        this.state = state;
        this.error = error;
        this.loading = false;
        this.changed.fire();
    }

    getTreeItem(item) {
        return item;
    }

    getChildren(item) {
        if (item) return item.children || [];
        if (this.loading) return [new Item("Asking worker.py ...", vscode.TreeItemCollapsibleState.None, { iconPath: new vscode.ThemeIcon("loading~spin") })];
        if (this.error) {
            return [new Item(this.error, vscode.TreeItemCollapsibleState.None, { iconPath: new vscode.ThemeIcon("error"), tooltip: this.error })];
        }
        const state = this.state;
        const items = [];
        if (!state.workers.length) {
            items.push(new Item("No workers", vscode.TreeItemCollapsibleState.None, {
                iconPath: new vscode.ThemeIcon("info"),
                description: "worker.py create 3",
                tooltip: `No worker folders in ${state.root}. Make them with \`python3 engine/tools/todo/worker.py create 3\` in the main checkout.`,
            }));
        }
        for (const worker of state.workers) items.push(this.workerItem(worker));
        items.push(this.reviewGroup(state));
        if (!state.sessionsKnown) {
            items.push(new Item("claude agents did not answer", vscode.TreeItemCollapsibleState.None, {
                iconPath: new vscode.ThemeIcon("warning"),
                tooltip: "Sessions are unknown: set todoWorkers.claude to the full path of claude.",
            }));
        }
        if (state.store !== "ok" && state.store !== "off") {
            items.push(new Item(`todo store: ${state.store}`, vscode.TreeItemCollapsibleState.None, { iconPath: new vscode.ThemeIcon("warning"), tooltip: state.store }));
        }
        return items;
    }

    workerItem(worker) {
        const entry = worker.entry;
        const label = `${worker.name} · ${entry ? entry.id : "no entry"}`;
        const description = [entry ? entry.area : "", sessionText(worker), progressText(entry), worker.dirty ? "dirty" : ""].filter(Boolean).join(" · ");
        const children = [];
        const line = (text, icon, extra) => children.push(new Item(text, vscode.TreeItemCollapsibleState.None, { iconPath: new vscode.ThemeIcon(icon), ...(extra || {}) }));
        if (entry) line(entry.title || entry.id, "note", { tooltip: `${entry.title}\n\nstatus: ${entry.status}${entry.statusNote ? ` (${entry.statusNote})` : ""}` });
        line(`game: ${headText(worker.head)}`, "git-branch");
        if (worker.engine) line(`engine: ${headText(worker.engine)}`, "git-branch");
        if (entry && progressText(entry)) line(`plan: ${progressText(entry)} boxes ticked`, "checklist");
        line(worker.path, "folder", { resourceUri: vscode.Uri.file(worker.path) });

        const context = [";worker;"];
        if (worker.session) context.push("session;");
        if (worker.active) context.push("active;");
        if (entry) context.push("entry;");
        const tooltip = new vscode.MarkdownString();
        tooltip.appendMarkdown(`**${worker.name}**${entry ? ` · \`${entry.id}\` (${entry.area}, ${entry.status})` : ""}\n\n`);
        if (entry) tooltip.appendText(`${entry.title}\n\n`);
        tooltip.appendMarkdown(`session: ${worker.session ? `\`${worker.session.name || worker.session.id}\` ${sessionText(worker)}` : "none"}\n\n`);
        tooltip.appendMarkdown(`game: \`${headText(worker.head)}\`${worker.engine ? ` · engine: \`${headText(worker.engine)}\`` : ""}${worker.dirty ? " · uncommitted changes" : ""}`);
        return new Item(label, vscode.TreeItemCollapsibleState.Collapsed, {
            id: `worker:${worker.name}`,
            description,
            tooltip,
            iconPath: workerIcon(worker),
            contextValue: context.join(""),
            children,
            worker,
            entry,
        });
    }

    reviewGroup(state) {
        const children = state.review.map((entry) => {
            const where = state.workers.find((w) => [w.head, w.engine].some((h) => h && h.branch === entry.branch));
            return new Item(entry.id, vscode.TreeItemCollapsibleState.None, {
                id: `review:${entry.id}`,
                description: [entry.area, entry.branch, where ? `in ${where.name}` : ""].filter(Boolean).join(" · "),
                tooltip: `${entry.title}${entry.statusNote ? `\n\n${entry.statusNote}` : ""}\n\nMerge it in the main checkout: /todo-merge ${entry.branch}`,
                iconPath: new vscode.ThemeIcon("git-pull-request", new vscode.ThemeColor("testing.iconPassed")),
                contextValue: ";entry;review;",
                entry,
            });
        });
        const label = state.store === "ok" ? `Review: ${children.length} to merge` : "Review: store unknown";
        return new Item(label, children.length ? vscode.TreeItemCollapsibleState.Expanded : vscode.TreeItemCollapsibleState.None, {
            id: "review",
            iconPath: new vscode.ThemeIcon("git-merge"),
            children,
        });
    }
}

function activate(context) {
    const provider = new WorkersProvider();
    const view = vscode.window.createTreeView("todoWorkers.view", { treeDataProvider: provider, showCollapseAll: true });
    const status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
    status.command = "workbench.view.extension.todoWorkers";
    context.subscriptions.push(view, status);

    let running = false;
    let timer = null;
    let previous = null; // {review: Set, active: Map}

    function notifyChanges(state) {
        const now = { review: new Set(state.review.map((e) => e.id)), active: new Map(state.workers.map((w) => [w.name, w.active])) };
        if (previous && config().notify) {
            for (const entry of state.review) {
                if (!previous.review.has(entry.id)) {
                    vscode.window.showInformationMessage(`Todo: ${entry.id} is in review (${entry.branch})`, "Copy /todo-merge").then((pick) => {
                        if (pick) vscode.env.clipboard.writeText(`/todo-merge ${entry.branch}`);
                    });
                }
            }
            for (const worker of state.workers) {
                if (previous.active.get(worker.name) && !worker.active) {
                    vscode.window.showInformationMessage(`Todo: the session of ${worker.name} has ended (${sessionText(worker)})`, "Show log").then((pick) => {
                        if (pick) openSessionTerminal(worker, "logs");
                    });
                }
            }
        }
        if (state.store === "ok" && state.sessionsKnown) previous = now;
    }

    function updateStatus(state) {
        if (!state) {
            status.text = "$(server-process) todo ?";
            status.tooltip = provider.error || "Todo Workers";
            status.show();
            return;
        }
        if (!state.workers.length) {
            status.hide();
            return;
        }
        const busy = state.workers.filter((w) => w.session && w.session.status === "busy").length;
        const waiting = state.workers.filter((w) => w.active && w.session && w.session.status !== "busy").length;
        const parts = [`${busy} busy`];
        if (waiting) parts.push(`${waiting} idle`);
        parts.push(`${state.review.length} review`);
        status.text = `$(server-process) todo ${parts.join(" · ")}`;
        status.tooltip = state.workers.map((w) => `${w.name}: ${w.entry ? w.entry.id : "no entry"} (${sessionText(w)})`).join("\n");
        status.backgroundColor = state.review.length ? new vscode.ThemeColor("statusBarItem.warningBackground") : undefined;
        status.show();
    }

    async function refresh() {
        if (running) return;
        const tool = findTool();
        if (!tool) {
            provider.set(null, "No tools/todo/worker.py in this workspace");
            status.hide();
            return;
        }
        running = true;
        try {
            const out = await runTool(tool, ["list", "--json", "--store", "--claude", config().claude], 90000);
            const state = JSON.parse(out);
            provider.set(state, null);
            updateStatus(state);
            notifyChanges(state);
        } catch (error) {
            provider.set(null, `worker.py list: ${error.message}`);
            updateStatus(null);
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

    function shellQuote(text) {
        return /^[A-Za-z0-9._\/-]+$/.test(text) ? text : `'${text.replace(/'/g, "'\\''")}'`;
    }

    function openSessionTerminal(worker, verb) {
        const session = worker.session;
        if (!session) return;
        const terminal = vscode.window.createTerminal({ name: `${worker.name} ${verb}`, cwd: worker.path, env: { PATH: toolEnv().PATH } });
        terminal.show();
        terminal.sendText(`${shellQuote(config().claude)} ${verb} ${shellQuote(session.name || session.id)}`);
    }

    const command = (name, handler) => context.subscriptions.push(vscode.commands.registerCommand(name, handler));
    command("todoWorkers.refresh", refresh);
    command("todoWorkers.attach", (item) => item && openSessionTerminal(item.worker, "attach"));
    command("todoWorkers.logs", (item) => item && openSessionTerminal(item.worker, "logs"));
    command("todoWorkers.openFolder", (item) => item && vscode.commands.executeCommand("vscode.openFolder", vscode.Uri.file(item.worker.path), { forceNewWindow: true }));
    command("todoWorkers.terminal", (item) => {
        if (!item) return;
        vscode.window.createTerminal({ name: item.worker.name, cwd: item.worker.path }).show();
    });
    command("todoWorkers.stop", async (item) => {
        if (!item) return;
        const pick = await vscode.window.showWarningMessage(
            `Stop the session of ${item.worker.name}? Its entry stays "in progress" and the next start continues it.`,
            { modal: true },
            "Stop"
        );
        if (pick !== "Stop") return;
        const tool = findTool();
        try {
            const out = await runTool(tool, ["stop", String(item.worker.k), "--claude", config().claude], 60000);
            vscode.window.showInformationMessage(out.trim().split("\n")[0] || `${item.worker.name} stopped`);
        } catch (error) {
            vscode.window.showErrorMessage(`worker.py stop: ${error.message}`);
        }
        refresh();
    });
    command("todoWorkers.copyMerge", async (item) => {
        if (!item || !item.entry) return;
        await vscode.env.clipboard.writeText(`/todo-merge ${item.entry.branch}`);
        vscode.window.showInformationMessage(`Copied: /todo-merge ${item.entry.branch} (paste it into Claude in the main checkout)`);
    });
    command("todoWorkers.openPlan", async (item) => {
        const file = planFile(item && item.entry);
        if (!file || !fs.existsSync(file)) {
            vscode.window.showWarningMessage(`No local plan ${file || ""}: python3 engine/tools/todo/todo.py plan get todo-loop-${branchId(item && item.entry && item.entry.branch)}.md`);
            return;
        }
        vscode.window.showTextDocument(vscode.Uri.file(file), { preview: true });
    });

    context.subscriptions.push(
        vscode.workspace.onDidChangeConfiguration((event) => {
            if (event.affectsConfiguration("todoWorkers")) {
                schedule();
                refresh();
            }
        }),
        vscode.workspace.onDidChangeWorkspaceFolders(refresh),
        view.onDidChangeVisibility((event) => event.visible && refresh()),
        { dispose: () => timer && clearInterval(timer) }
    );
    schedule();
    refresh();
}

function deactivate() {}

module.exports = { activate, deactivate };
