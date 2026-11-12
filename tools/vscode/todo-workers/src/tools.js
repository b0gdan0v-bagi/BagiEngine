// The bridge to the tools of the checkout: the settings, worker.py and todo.py, git, the PATH they need, and the
// webview page shell. No state: every other module builds on these.
"use strict";

const vscode = require("vscode");
const childProcess = require("child_process");
const crypto = require("crypto");
const fs = require("fs");
const os = require("os");
const path = require("path");

const TOOL_PATHS = [path.join("engine", "tools", "todo", "worker.py"), path.join("tools", "todo", "worker.py")];

// API list prices in dollars per million tokens (cache write: the 5-minute write, 1.25 x input). The first key that is
// part of a model id wins. A plan's subscription bills otherwise: the panel's dollars are what the same tokens cost on the API.
// worker.py cost has the same table (DEFAULT_PRICES there).
const DEFAULT_PRICES = {
    fable: { input: 8, output: 40, cacheWrite: 10, cacheRead: 0.2 },
    mythos: { input: 8, output: 40, cacheWrite: 10, cacheRead: 0.2 },
    opus: { input: 4, output: 20, cacheWrite: 5, cacheRead: 0.2 },
    sonnet: { input: 2, output: 10, cacheWrite: 2.5, cacheRead: 0.1 },
    haiku: { input: 0.1, output: 0.5, cacheWrite: 0.125, cacheRead: 0.01 },
};

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
        autoStartQueue: c.get("autoStartQueue", true),
        preset: c.get("preset", ""),
        prices: c.get("prices", DEFAULT_PRICES),
    };
}

// VS Code started from the Dock does not see the PATH of a login shell, where `claude` usually is. The extra
// folders go before the system ones, not after: `/usr/bin/python3` of macOS is 3.9 and worker.py needs 3.10, so a
// bare `python3` must find Homebrew's first (execFile looks the command up in options.env.PATH). The owner's own
// folders keep their order ahead of the system ones.
const SYSTEM_DIRS = ["/usr/bin", "/bin", "/usr/sbin", "/sbin"];
function toolEnv() {
    const home = os.homedir();
    const extra = [path.join(home, ".local", "bin"), path.join(home, ".claude", "local"), "/opt/homebrew/bin", "/usr/local/bin"];
    const parts = (process.env.PATH || "").split(path.delimiter).filter(Boolean);
    const firstSystem = parts.findIndex((dir) => SYSTEM_DIRS.includes(dir));
    const missing = extra.filter((dir) => !parts.includes(dir));
    parts.splice(firstSystem < 0 ? parts.length : firstSystem, 0, ...missing);
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

function todoTool(tool) {
    return { cwd: tool.cwd, script: path.join(path.dirname(tool.script), "todo.py") };
}

// `input`: text written to the tool's stdin (todo.py ... --body-file -).
function runTool(tool, args, timeout, input) {
    const { python } = config();
    return new Promise((resolve, reject) => {
        const child = childProcess.execFile(python, [tool.script, ...args], { cwd: tool.cwd, env: toolEnv(), timeout, maxBuffer: 16 * 1024 * 1024 }, (error, stdout, stderr) => {
            if (error) {
                const text = (stderr || stdout || error.message).trim().split("\n").pop();
                reject(new Error(text || error.message));
            } else {
                resolve(stdout);
            }
        });
        if (input !== undefined) child.stdin.end(input);
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

// The local copy of the plan the entry shows (its newest slice plan), else of its first one.
function planFile(entry) {
    if (!entry || !entry.branch) return null;
    const name = entry.planShown || `todo-loop-${branchId(entry.branch)}.md`;
    return path.join(os.homedir(), ".claude", "plans", name);
}

function shellQuote(text) {
    return /^[A-Za-z0-9._\/-]+$/.test(text) ? text : `'${text.replace(/'/g, "'\\''")}'`;
}

function sessionWord(worker) {
    const s = worker.session;
    if (!s) return "no session";
    if (worker.limited) return "limited";
    if (worker.stopped) return "stopped"; // worker.py stop ended it (its entry stays in progress)
    if (s.state === "done" || worker.endedTurn) return "done"; // a run that ended its turn waits for nothing
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

// A terminal running one command, or the open one of the same name.
function terminalWith(name, cwd, command) {
    const open = vscode.window.terminals.find((t) => t.name === name && t.exitStatus === undefined);
    if (open) {
        open.show();
        return open;
    }
    const terminal = vscode.window.createTerminal({ name, cwd, env: { PATH: toolEnv().PATH } });
    terminal.show();
    terminal.sendText(command);
    return terminal;
}

module.exports = { DEFAULT_PRICES, config, toolEnv, findTool, todoTool, runTool, runGit, branchId, planFile, shellQuote, sessionWord, pageHtml, terminalWith };
