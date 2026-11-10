// The panel of the parallel todo-loop workers (tools/todo/worker.py, "Workers" in tools/todo/CLAUDE.md).
// It reads `worker.py list --json --store --activity` and shows one card per worker (its entry, the state
// of its session, the plan, the branches, what the session and its agents do now) and the entries in
// review. It reads, opens terminals and diffs; the one write is a merge the owner confirms (Quick merge runs
// worker.py merge, Merge with Claude a terminal with claude '/todo-merge <branch>'). Review with Claude opens a
// terminal with a read-only claude review of the branch. Update (worker.py update) moves an idle worker to the
// tips of the bases, Fast start (worker.py start <k>) starts its loop at once; /todo-loop-parallel starts them all.
// Todo list opens a tab with the entries of the todo store (todo.py list), filtered by area, status and text;
// Attach to free worker there starts `/todo-loop 1 <id>` in a free worker (worker.py start <k>). Both views read
// again at once when todo.py signals a write (~/.claude/todo/changed.json), besides the timer.
"use strict";

const vscode = require("vscode");
const childProcess = require("child_process");
const crypto = require("crypto");
const fs = require("fs");
const os = require("os");
const path = require("path");

const STORE_CHANGE_DEBOUNCE_MS = 700; // a claim and its stub plan are two writes: one refresh

const TOOL_PATHS = [path.join("engine", "tools", "todo", "worker.py"), path.join("tools", "todo", "worker.py")];

function config() {
    const c = vscode.workspace.getConfiguration("todoWorkers");
    return {
        python: c.get("python", "python3"),
        claude: c.get("claude", "claude"),
        refreshSeconds: Math.max(5, c.get("refreshSeconds", 15)),
        notify: c.get("notify", true),
        startSlices: Math.max(1, Math.floor(c.get("startSlices", 1))),
        loopArgs: c.get("loopArgs", ""),
        reportTo: c.get("reportTo", ""),
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

function runGit(cwd, args, timeout) {
    return new Promise((resolve, reject) => {
        childProcess.execFile("git", args, { cwd, env: toolEnv(), timeout: timeout || 30000, maxBuffer: 16 * 1024 * 1024 }, (error, stdout, stderr) => {
            if (error) reject(new Error((stderr || error.message).trim()));
            else resolve(stdout);
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
    if (worker.limited) return "limited";
    if (worker.stopped) return "stopped"; // worker.py stop ended it (its entry stays in progress)
    if (s.state === "done") return "done";
    if (worker.waiting) return "waiting";
    return s.status === "busy" ? "busy" : "idle";
}

// A webview page: the script and the styles of media/ inline, under a nonce.
function pageHtml(context, scriptName, styleNames, placeholder) {
    const nonce = crypto.randomBytes(16).toString("hex");
    const read = (name) => fs.readFileSync(path.join(context.extensionPath, "media", name), "utf8");
    const style = styleNames.map(read).join("\n");
    return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'nonce-${nonce}'; script-src 'nonce-${nonce}';">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style nonce="${nonce}">${style}</style>
</head>
<body>
<div id="root"><p class="muted">${placeholder}</p></div>
<script nonce="${nonce}">${read(scriptName)}</script>
</body>
</html>`;
}

function todoTool(tool) {
    return { cwd: tool.cwd, script: path.join(path.dirname(tool.script), "todo.py") };
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

    html() {
        return pageHtml(this.context, "panel.js", ["panel.css"], "Asking worker.py ...");
    }
}

function activate(context) {
    const status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
    status.command = "workbench.view.extension.todoWorkers";
    context.subscriptions.push(status);

    let state = null; // the last answer of worker.py
    let running = false;
    let timer = null;
    let previous = null; // {review: Set, active: Map, waiting: Set}

    const workerOf = (k) => state && state.workers.find((w) => w.k === k);
    const entryOf = (id) => state && [...state.review, ...state.workers.map((w) => w.entry)].find((e) => e && e.id === id);

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
        refresh();
    }

    // Update: worker.py update <k>, the loop's "go to the base" for an idle worker (every repository detached
    // at the tip of its base branch; the branch it held is kept). Refused for a running session or changes.
    const busy = new Set();
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
            refresh();
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
            refresh();
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
            refresh();
        }
    }

    async function openPlan(entry) {
        const file = planFile(entry);
        if (!file || !fs.existsSync(file)) {
            vscode.window.showWarningMessage(`No local plan ${file || ""}: python3 engine/tools/todo/todo.py plan get todo-loop-${branchId(entry && entry.branch)}.md`);
            return;
        }
        vscode.window.showTextDocument(vscode.Uri.file(file), { preview: true });
    }

    // Review: the branch against the point where it left its base, every file in one multi-diff editor
    // (the git extension's URIs); a terminal with the diff when the editor cannot show it.
    async function reviewBranch(entry) {
        if (notHere(entry)) return;
        if (!entry || !entry.branch || !entry.repo || !entry.base) {
            vscode.window.showWarningMessage("No repository or base for this entry (the Areas table of .claude/rules/todo-project.md)");
            return;
        }
        const title = `${entry.id}: ${entry.base}...${entry.branch}`;
        try {
            const mergeBase = (await runGit(entry.repo, ["merge-base", entry.base, entry.branch])).trim();
            const gitExtension = vscode.extensions.getExtension("vscode.git");
            const gitApi = (gitExtension.isActive ? gitExtension.exports : await gitExtension.activate()).getAPI(1);
            const lines = (await runGit(entry.repo, ["diff", "--name-status", "-M", mergeBase, entry.branch])).split("\n").filter(Boolean);
            const resources = lines.map((line) => {
                const parts = line.split("\t");
                const kind = parts[0][0];
                const before = vscode.Uri.file(path.join(entry.repo, parts[1]));
                const after = vscode.Uri.file(path.join(entry.repo, parts[parts.length - 1]));
                return [after, kind === "A" ? undefined : gitApi.toGitUri(before, mergeBase), kind === "D" ? undefined : gitApi.toGitUri(after, entry.branch)];
            });
            if (!resources.length) {
                vscode.window.showInformationMessage(`${title}: no changes`);
                return;
            }
            await vscode.commands.executeCommand("vscode.changes", title, resources);
        } catch (error) {
            const terminal = vscode.window.createTerminal({ name: `review ${entry.id}`, cwd: entry.repo });
            terminal.show();
            terminal.sendText(`git log --oneline ${shellQuote(entry.base)}..${shellQuote(entry.branch)}; git diff --stat ${shellQuote(entry.base)}...${shellQuote(entry.branch)}`);
            vscode.window.showWarningMessage(`The diff editor did not open (${error.message}); the terminal shows the diff instead`);
        }
    }

    // Quick merge: worker.py merge, without Claude. The dialog shows the dry run; a yes merges (--no-ff),
    // closes the entry (or opens it again for the next slice), frees and deletes the branch and, for the
    // engine in a game, commits the pointer. Conflicts and a dirty checkout are refused by worker.py.
    const merging = new Set();
    async function quickMerge(entry) {
        const tool = findTool();
        if (!entry || !tool || merging.has(entry.id) || notHere(entry)) return;
        merging.add(entry.id);
        try {
            let plan;
            try {
                plan = JSON.parse(await runTool(tool, ["merge", entry.id, "--dry-run", "--json"], 90000));
            } catch (error) {
                vscode.window.showErrorMessage(`Quick merge of ${entry.id}: ${error.message}`);
                return;
            }
            if (plan.conflicts.length || plan.problems.length) {
                const why = plan.conflicts.length ? `conflicts with ${plan.base} in ${plan.conflicts.join(", ")}` : plan.problems.join("; ");
                vscode.window.showErrorMessage(`Quick merge of ${plan.branch}: ${why}`, { modal: true, detail: "Nothing was changed. Conflicts are resolved by hand." });
                return;
            }
            const detail = [`${plan.commits.length} commits into ${plan.base}: ${plan.stat}`, ...plan.commits.slice(0, 8).map((line) => `  ${line}`)];
            if (plan.commits.length > 8) detail.push(`  ... and ${plan.commits.length - 8} more`);
            detail.push("");
            if (plan.moved) detail.push("The base moved since the branch was cut: the merged result is not built or tested here. Run the tests after it.");
            detail.push(plan.reopen ? "The entry is a slice: it goes back to open for the next one." : "The entry is closed.");
            if (plan.heldBy) detail.push(`${plan.heldBy} lets go of the branch (detached at ${plan.base}); the branch is deleted.`);
            else detail.push("The branch is deleted.");
            if (plan.pointer) detail.push(`Then one commit in the game: "Update engine: ${plan.title}".`);
            detail.push("Nothing is pushed.");
            const pick = await vscode.window.showWarningMessage(`Merge ${plan.branch} into ${plan.base}?`, { modal: true, detail: detail.join("\n") }, "Merge");
            if (pick !== "Merge") return;
            try {
                const result = JSON.parse(await runTool(tool, ["merge", entry.id, "--json"], 120000));
                const parts = [`Merged ${result.branch} into ${result.base} (${result.merge})`, `entry ${result.status || "still in review"}`];
                if (result.pointer) parts.push(`engine pointer ${result.pointer}`);
                const text = parts.join(", ");
                if (result.notes.length) vscode.window.showWarningMessage(`${text}. ${result.notes.join("; ")}`);
                else vscode.window.showInformationMessage(text);
            } catch (error) {
                vscode.window.showErrorMessage(`Quick merge of ${entry.id}: ${error.message}`);
            }
        } finally {
            merging.delete(entry.id);
            refresh();
        }
    }

    // /todo-merge needs the owner's answer and the main checkout, so the button starts an interactive
    // Claude there with the skill; the skill reports, asks and merges. The extension itself writes nothing.
    // An entry whose branch is not here (taken in another checkout or on another machine) or that was cut
    // from another base (worker.py list --store: local, foreignBase): nothing to review or merge here.
    function notHere(entry) {
        if (!entry || (entry.local !== false && !entry.foreignBase)) return false;
        vscode.window.showWarningMessage(`${entry.id}: ${whereNot(entry)}`);
        return true;
    }

    // The stored base and origin (todo.py claim --base) when the entry has them, else the commit guess.
    function whereNot(entry) {
        if (entry.foreignBase && entry.takenBase) {
            return `${entry.branch} was taken in ${entry.takenOrigin || "another checkout"}, base ${entry.takenBase}, not ${entry.base}: review and merge it in the checkout that made it`;
        }
        if (entry.foreignBase) {
            return `${entry.branch} was cut from ${entry.foreignBase}, not from ${entry.base}: review and merge it in the checkout that made it`;
        }
        return entry.takenOrigin
            ? `no branch ${entry.branch} in this checkout: it was taken in ${entry.takenOrigin}, review and merge it there`
            : `no branch ${entry.branch} in this checkout: it was taken in another checkout or on another machine, review and merge it there`;
    }

    function startMerge(entry) {
        const tool = findTool();
        if (!entry || !entry.branch || !tool || notHere(entry)) return;
        const name = `todo-merge ${entry.id}`;
        const open = vscode.window.terminals.find((t) => t.name === name && t.exitStatus === undefined);
        if (open) {
            open.show();
            return;
        }
        const terminal = vscode.window.createTerminal({ name, cwd: tool.cwd, env: { PATH: toolEnv().PATH } });
        terminal.show();
        terminal.sendText(`${shellQuote(config().claude)} ${shellQuote(`/todo-merge ${entry.branch}`)}`);
    }

    // Review with Claude: an interactive claude in the main checkout that reads the entry and the diff of the
    // branch against its base and reports findings. Read only: it is told not to check out, edit, commit or merge.
    function startClaudeReview(entry) {
        const tool = findTool();
        if (!entry || !entry.branch || !entry.base || !tool || notHere(entry)) return;
        const name = `todo-review ${entry.id}`;
        const open = vscode.window.terminals.find((t) => t.name === name && t.exitStatus === undefined);
        if (open) {
            open.show();
            return;
        }
        const repo = path.relative(tool.cwd, entry.repo || tool.cwd) || ".";
        const todo = path.relative(tool.cwd, path.join(path.dirname(tool.script), "todo.py"));
        const prompt = [
            `Review the branch ${entry.branch} of the todo entry ${entry.id} (area ${entry.area}) before it is merged into ${entry.base}.`,
            `The repository is ${repo}: the changes are \`git -C ${repo} diff ${entry.base}...${entry.branch}\`, the commits \`git -C ${repo} log ${entry.base}..${entry.branch}\`.`,
            `Read the entry and its plan first: \`python3 ${todo} show ${entry.id}\` (and \`plan get\` when it links one).`,
            "Check the changes against the entry and against the CLAUDE.md files and .claude/rules of the repository: correctness bugs, missing or weak tests, docs that no longer match, work the entry asks for that is missing.",
            "Report the findings ranked by severity with file:line, then a verdict: merge as is, or what to fix first.",
            "Read only: do not check out, edit, commit, merge or change the todo store.",
        ].join(" ");
        const terminal = vscode.window.createTerminal({ name, cwd: tool.cwd, env: { PATH: toolEnv().PATH } });
        terminal.show();
        terminal.sendText(`${shellQuote(config().claude)} ${shellQuote(prompt)}`);
    }

    // The Todo list tab: the entries of the store (todo.py list --all --json --body), read again on Refresh.
    let todoPanel = null;

    let todoLoading = false;
    let todoAgain = false;

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
            const entries = JSON.parse(await runTool(todoTool(tool), ["list", "--all", "--json", "--body"], 30000));
            if (todoPanel) todoPanel.webview.postMessage({ loading: false, entries, areas: state && state.areas ? state.areas : null, workers: workersForList(), elsewhere: elsewhereForList(), at: Date.now() });
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
        const file = path.join(os.homedir(), ".claude", "plans", name);
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

    // What the Todo list tab needs of the workers: who is free, who works which entry.
    function workersForList() {
        if (!state) return null;
        return state.workers.map((w) => ({ k: w.k, name: w.name, active: Boolean(w.active), dirty: Boolean(w.dirty), busy: busy.has(w.k), entry: w.entry ? w.entry.id : null }));
    }

    // The ids of the entries whose branch lives in another checkout (worker.py list: elsewhere).
    function elsewhereForList() {
        return state && Array.isArray(state.elsewhere) ? state.elsewhere : [];
    }

    function postWorkersToList() {
        if (todoPanel) todoPanel.webview.postMessage({ workers: workersForList(), elsewhere: elsewhereForList() });
    }

    // A free worker: no session running, no uncommitted changes, no button of this panel at work on it.
    // The ones that hold no entry come first.
    function freeWorkers() {
        if (!state) return [];
        return state.workers
            .filter((w) => !w.active && !w.dirty && !busy.has(w.k))
            .sort((a, b) => (a.entry ? 1 : 0) - (b.entry ? 1 : 0) || a.k - b.k);
    }

    // Attach to free worker: `/todo-loop 1 <id>` (with the loop arguments and the report session of the
    // settings) as the background session of a free worker, after the owner picks the worker.
    async function startEntryInWorker(id) {
        const tool = findTool();
        if (!tool || !id || !/^[A-Za-z0-9_-]+$/.test(id)) return;
        if (!state) await refresh();
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
        const loopArgs = [id, c.loopArgs].filter(Boolean).join(" ");
        const label = (w) => (w.entry && w.entry.id !== id ? `${w.name} (leaves ${w.entry.id})` : w.entry ? `${w.name} (holds its branch)` : w.name);
        const picked = await vscode.window.showInformationMessage(
            `Start /todo-loop 1 ${loopArgs} in a free worker?`,
            { modal: true, detail: "It runs as the background session todo-w<k> of that worker; the loop goes to the base itself. Attach opens it when it asks for a permission." },
            ...free.map(label)
        );
        const worker = free.find((w) => label(w) === picked);
        if (!worker || busy.has(worker.k)) return;
        const args = ["start", String(worker.k), "--delay", "0", "--slices", "1", "--loop-args", loopArgs, "--claude", c.claude];
        if (c.reportTo) args.push("--report-to", c.reportTo);
        busy.add(worker.k);
        postWorkersToList();
        try {
            const out = await runTool(tool, args, 90000);
            const line = out.split("\n").map((l) => l.trim()).find((l) => l.startsWith("backgrounded") || l.includes("left alone")) || `${worker.name} started`;
            vscode.window.showInformationMessage(`${worker.name} · ${id}: ${line}`);
        } catch (error) {
            vscode.window.showErrorMessage(`${worker.name} · ${id}: ${error.message}`);
        } finally {
            busy.delete(worker.k);
            await refresh();
        }
    }

    function openTodoList() {
        if (todoPanel) {
            todoPanel.reveal();
            return loadTodoList();
        }
        todoPanel = vscode.window.createWebviewPanel("todoWorkers.todoList", "Todo list", vscode.ViewColumn.Active, { enableScripts: true, retainContextWhenHidden: true });
        todoPanel.iconPath = vscode.Uri.file(path.join(context.extensionPath, "media", "workers.svg"));
        todoPanel.webview.html = pageHtml(context, "todo.js", ["panel.css", "todo.css"], "Asking todo.py ...");
        todoPanel.webview.onDidReceiveMessage((message) => {
            switch (message.cmd) {
                case "ready":
                case "refresh": return loadTodoList();
                case "show": return showTodoEntry(message.entry);
                case "plan": return openTodoPlan(message.name);
                case "copy": return vscode.env.clipboard.writeText(String(message.text || ""));
                case "startInWorker": return startEntryInWorker(message.entry);
            }
        });
        todoPanel.onDidChangeViewState((event) => {
            if (event.webviewPanel.visible) loadTodoList();
        });
        todoPanel.onDidDispose(() => {
            todoPanel = null;
        });
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
            case "update": return updateWorker(worker);
            case "fastStart": return fastStart(worker);
            case "relaunch": return relaunch(worker);
            case "relaunchAll": return relaunch(null);
            case "plan": return openPlan(entryOf(message.entry));
            case "merge": return startMerge(entryOf(message.entry));
            case "quickMerge": return quickMerge(entryOf(message.entry));
            case "review": return reviewBranch(entryOf(message.entry));
            case "claudeReview": return startClaudeReview(entryOf(message.entry));
            case "todoList": return openTodoList();
            case "settings": return vscode.commands.executeCommand("workbench.action.openSettings", "todoWorkers");
        }
    }

    const view = new WorkersView(context, act);
    context.subscriptions.push(vscode.window.registerWebviewViewProvider("todoWorkers.view", view, { webviewOptions: { retainContextWhenHidden: true } }));

    function notifyChanges() {
        const now = {
            review: new Set(state.review.map((e) => e.id)),
            active: new Map(state.workers.map((w) => [w.name, w.active])),
            waiting: new Set(state.workers.filter((w) => w.waiting).map((w) => w.name)),
            limited: new Set(state.workers.filter((w) => w.limited).map((w) => w.name)),
        };
        if (previous && config().notify) {
            for (const entry of state.review) {
                if (!previous.review.has(entry.id)) {
                    vscode.window.showInformationMessage(`Todo: ${entry.id} is in review (${entry.branch})`, "Review", "Review with Claude", "Quick merge", "Merge with Claude").then((pick) => {
                        if (pick === "Review") reviewBranch(entry);
                        else if (pick === "Review with Claude") startClaudeReview(entry);
                        else if (pick === "Quick merge") quickMerge(entry);
                        else if (pick === "Merge with Claude") startMerge(entry);
                    });
                }
            }
            for (const worker of state.workers) {
                // A loop that asks a question or a permission stops until someone answers: Answer opens it.
                if (worker.waiting && !previous.waiting.has(worker.name)) {
                    const what = (worker.session && worker.session.waitingFor) || "input needed";
                    const about = worker.entry ? ` (${worker.entry.id})` : "";
                    vscode.window.showWarningMessage(`Todo: ${worker.name}${about} waits for you: ${what}`, "Answer").then((pick) => pick && openSessionTerminal(worker, "attach"));
                }
                // The usage limit stops a loop until it resets: Relaunch continues it then (the button stays on the card).
                if (worker.limited && !previous.limited.has(worker.name)) {
                    const when = worker.limited.resets ? `, resets ${worker.limited.resets}` : "";
                    vscode.window.showWarningMessage(`Todo: ${worker.name} was stopped by the usage limit${when}. Relaunch it after the reset.`, "Relaunch").then((pick) => pick && relaunch(workerOf(worker.k) || worker));
                }
                if (previous.active.get(worker.name) && !worker.active && !worker.limited && !worker.stopped) {
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
        const parts = [];
        if (count("waiting")) parts.push(`${count("waiting")} waits for you`);
        if (count("limited")) parts.push(`${count("limited")} hit the limit`);
        parts.push(`${count("busy")} busy`);
        if (count("idle")) parts.push(`${count("idle")} idle`);
        if (count("stopped")) parts.push(`${count("stopped")} stopped`);
        parts.push(`${state.review.length} review`);
        status.text = `$(server-process) todo ${parts.join(" · ")}`;
        const lines = state.workers.map((w) => `${w.name}: ${w.entry ? w.entry.id : "no entry"} (${sessionWord(w)})`);
        if (state.usage && state.usage.total.total) {
            const m = (n) => `${((n || 0) / 1e6).toFixed(1)}M`;
            lines.push(`tokens: ${m(state.usage.total.total)}, ${m(state.usage.recent.total)} in the last ${state.usage.recentMinutes} min`);
        }
        status.tooltip = lines.join("\n");
        status.backgroundColor = count("waiting")
            ? new vscode.ThemeColor("statusBarItem.errorBackground")
            : state.review.length || count("limited")
              ? new vscode.ThemeColor("statusBarItem.warningBackground")
              : undefined;
        status.show();
    }

    let refreshAgain = false;

    async function refresh() {
        if (running) {
            refreshAgain = true;
            return;
        }
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
            const out = await runTool(tool, ["list", "--json", "--store", "--activity", "--usage", "--claude", config().claude], 90000);
            state = JSON.parse(out);
            view.show({ state, at: Date.now() });
            postWorkersToList();
            updateStatus(null);
            notifyChanges();
        } catch (error) {
            view.show({ ...(state ? { state } : {}), error: `worker.py list: ${error.message}`, at: Date.now() });
            if (!state) updateStatus(error.message);
        } finally {
            running = false;
            if (refreshAgain) {
                refreshAgain = false;
                refresh();
            }
        }
    }

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
                    loadTodoList();
                }, STORE_CHANGE_DEBOUNCE_MS);
            });
            watcher.on("error", () => watcher.close());
            context.subscriptions.push({ dispose: () => { clearTimeout(pending); watcher.close(); } });
        } catch (error) {
            // no hook (no folder, no watcher): the timer still refreshes
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
    watchStoreChanges();
    refresh();
}

function deactivate() {}

module.exports = { activate, deactivate };
