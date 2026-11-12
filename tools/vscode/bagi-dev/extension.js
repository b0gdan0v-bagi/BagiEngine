// Bagi Dev: one tree to build and run the products of a BagiEngine repository. Everything goes through
// engine/tools/dev/dev.py (tools/dev/CLAUDE.md), the same command line the terminal and the agents use: the tree reads
// `dev.py info --json` (presets, flags, products, chains, tools of dev.json), a flag is `dev.py flags` (the presets
// <preset>-dev in CMakeUserPresets.json), and Build / Run / a chain / a tool open a terminal with the dev.py command.
"use strict";

const vscode = require("vscode");
const childProcess = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");

const DEV_PATHS = [path.join("engine", "tools", "dev", "dev.py"), path.join("tools", "dev", "dev.py")];
const PICKED_KEY = "bagiDev.picked"; // {desktop, web}: the presets the owner picked, per workspace

// VS Code started from the Dock does not see the PATH of a login shell: Homebrew and ~/.local/bin go before the
// system folders (macOS's /usr/bin/python3 is old).
const SYSTEM_DIRS = ["/usr/bin", "/bin", "/usr/sbin", "/sbin"];
function toolEnv() {
    const home = os.homedir();
    const extra = [path.join(home, ".local", "bin"), "/opt/homebrew/bin", "/usr/local/bin"];
    const parts = (process.env.PATH || "").split(path.delimiter).filter(Boolean);
    const firstSystem = parts.findIndex((dir) => SYSTEM_DIRS.includes(dir));
    parts.splice(firstSystem < 0 ? parts.length : firstSystem, 0, ...extra.filter((dir) => !parts.includes(dir)));
    return { ...process.env, PATH: parts.join(path.delimiter) };
}

function findDev() {
    for (const folder of vscode.workspace.workspaceFolders || []) {
        for (const rel of DEV_PATHS) {
            if (fs.existsSync(path.join(folder.uri.fsPath, rel))) return { root: folder.uri.fsPath, rel };
        }
    }
    return null;
}

function python() {
    return vscode.workspace.getConfiguration("bagiDev").get("python", "python3");
}

function shellQuote(text) {
    return /^[A-Za-z0-9._\/=:-]+$/.test(text) ? text : `'${String(text).replace(/'/g, "'\\''")}'`;
}

function runDev(dev, args) {
    return new Promise((resolve, reject) => {
        childProcess.execFile(python(), [dev.rel, ...args], { cwd: dev.root, env: toolEnv(), timeout: 60000, maxBuffer: 8 * 1024 * 1024 }, (error, stdout, stderr) => {
            if (error) reject(new Error((stderr || stdout || error.message).trim().split("\n").pop()));
            else resolve(stdout);
        });
    });
}

// The value a flag has in a preset: its dev preset's, else the flag's default, else OFF.
function flagValue(info, preset, flag) {
    const dev = info.devPresets[preset.endsWith("-dev") ? preset : `${preset}-dev`];
    const set = dev && dev.flags[flag.name];
    return set !== undefined ? String(set) : flag.default || "OFF";
}

class Tree {
    constructor(context) {
        this.context = context;
        this.info = null;
        this.error = null;
        this.changed = new vscode.EventEmitter();
        this.onDidChangeTreeData = this.changed.event;
    }

    picked(kind) {
        const saved = this.context.workspaceState.get(PICKED_KEY, {});
        const groups = this.info ? this.info.presets : {};
        const known = [...(groups[kind] || []), ...(groups.user || [])];
        return saved[kind] && known.includes(saved[kind]) ? saved[kind] : this.info && this.info.defaults[kind];
    }

    async pick(kind, preset) {
        const saved = { ...this.context.workspaceState.get(PICKED_KEY, {}), [kind]: preset };
        await this.context.workspaceState.update(PICKED_KEY, saved);
        this.changed.fire();
    }

    async refresh() {
        const dev = findDev();
        if (!dev) {
            this.info = null;
            this.error = "No tools/dev/dev.py in this workspace";
        } else {
            try {
                this.info = JSON.parse(await runDev(dev, ["info", "--json"]));
                this.error = null;
            } catch (error) {
                this.error = `dev.py info: ${error.message}`;
            }
        }
        this.changed.fire();
    }

    getTreeItem(node) {
        return node.item;
    }

    getChildren(node) {
        if (!this.info) return [leaf(this.error || "Reading dev.py ...", "", "info")];
        const info = this.info;
        if (!node) {
            const desktop = this.picked("desktop");
            const web = this.picked("web");
            const roots = [
                group("desktop", `Desktop: ${desktop || "no preset"}`, "the preset of the desktop products; click to pick another (the -dev ones carry flags)", "device-desktop", { command: "bagiDev.pickPreset", title: "Pick", arguments: ["desktop"] }),
                group("web", `Web: ${web || "no preset"}`, "the preset of the web products", "globe", { command: "bagiDev.pickPreset", title: "Pick", arguments: ["web"] }, false),
                group("products", "Products", "build or run one (the inline buttons; a click runs it)", "package"),
            ];
            if (info.chains.length) roots.push(group("chains", "Chains", "several products started together", "run-all"));
            if (info.tools.length) roots.push(group("tools", "Tools", "the repository's tools", "tools"));
            if (!info.manifest) roots.push(leaf("No dev.json: open it to describe the products", "", "warning", { command: "bagiDev.openManifest", title: "Open" }));
            return roots;
        }
        switch (node.id) {
            case "desktop": {
                const preset = this.picked("desktop");
                if (!preset) return [];
                return info.flags.map((flag) => {
                    const on = flagValue(info, preset, flag) === "ON";
                    const item = new vscode.TreeItem(flag.name, vscode.TreeItemCollapsibleState.None);
                    item.description = flag.label || "";
                    item.iconPath = new vscode.ThemeIcon(on ? "pass-filled" : "circle-large-outline");
                    item.tooltip = `${flag.name}=${on ? "ON" : "OFF"} in ${preset}. Click to switch: the flags live in the preset ${preset.replace(/-dev$/, "")}-dev of CMakeUserPresets.json, built in its own folder.`;
                    item.command = { command: "bagiDev.toggleFlag", title: "Toggle", arguments: [flag.name] };
                    return { id: `flag:${flag.name}`, item };
                });
            }
            case "products":
                return info.products.map((p) => {
                    const kind = p.kind || "desktop";
                    const preset = this.presetFor(p);
                    const item = new vscode.TreeItem(p.label || p.name, vscode.TreeItemCollapsibleState.None);
                    item.description = `${p.name} · ${preset || kind}`;
                    item.tooltip = [p.label || p.name, `targets: ${(p.targets || [p.target]).filter(Boolean).join(" ") || "-"}`, `run: ${(p.run || []).join(" ") || "-"}`, p.url ? `then open ${p.url}` : ""].filter(Boolean).join("\n");
                    item.iconPath = new vscode.ThemeIcon(kind === "web" ? "globe" : "symbol-event");
                    item.contextValue = "product";
                    item.command = { command: "bagiDev.run", title: "Run", arguments: [{ product: p.name }] };
                    return { id: `product:${p.name}`, product: p.name, item };
                });
            case "chains":
                return info.chains.map((c) => {
                    const item = new vscode.TreeItem(c.label || c.name, vscode.TreeItemCollapsibleState.None);
                    item.description = c.steps.map((s) => (typeof s === "string" ? s : s.product)).join(" + ");
                    item.iconPath = new vscode.ThemeIcon("run-all");
                    item.contextValue = "chain";
                    item.command = { command: "bagiDev.chain", title: "Run", arguments: [{ chain: c.name }] };
                    return { id: `chain:${c.name}`, chain: c.name, item };
                });
            case "tools":
                return info.tools.map((t) => {
                    const item = new vscode.TreeItem(t.label || t.name, vscode.TreeItemCollapsibleState.None);
                    item.description = t.name;
                    item.tooltip = t.command.join(" ");
                    item.iconPath = new vscode.ThemeIcon("tools");
                    item.contextValue = "tool";
                    item.command = { command: "bagiDev.tool", title: "Run", arguments: [{ tool: t.name }] };
                    return { id: `tool:${t.name}`, tool: t.name, item };
                });
        }
        return [];
    }

    presetFor(product) {
        const kind = product.kind || "desktop";
        const picked = this.picked(kind);
        // a product that prefers Release (the arena) takes it unless the owner picked another preset
        const saved = this.context.workspaceState.get(PICKED_KEY, {})[kind];
        if (!saved && product.prefer === "release" && this.info) {
            const wanted = kind === "web" ? "web-release" : (picked || "").replace(/-debug$/, "-release");
            if ((this.info.presets[kind] || []).includes(wanted)) return wanted;
        }
        return picked;
    }
}

function group(id, label, tooltip, icon, command, expanded = true) {
    const item = new vscode.TreeItem(label, expanded ? vscode.TreeItemCollapsibleState.Expanded : vscode.TreeItemCollapsibleState.None);
    item.tooltip = tooltip;
    item.iconPath = new vscode.ThemeIcon(icon);
    if (command) item.command = command;
    return { id, item };
}

function leaf(label, description, icon, command) {
    const item = new vscode.TreeItem(label, vscode.TreeItemCollapsibleState.None);
    item.description = description;
    item.iconPath = new vscode.ThemeIcon(icon);
    if (command) item.command = command;
    return { id: `leaf:${label}`, item };
}

function activate(context) {
    const tree = new Tree(context);
    const view = vscode.window.createTreeView("bagiDev.tree", { treeDataProvider: tree });
    context.subscriptions.push(view);

    // A command in its own terminal: a new run of the same thing replaces the old terminal (Ctrl+C stops a game).
    function inTerminal(name, args) {
        const dev = findDev();
        if (!dev) return;
        const old = vscode.window.terminals.find((t) => t.name === name);
        if (old) old.dispose();
        const terminal = vscode.window.createTerminal({ name, cwd: dev.root, env: { PATH: toolEnv().PATH } });
        terminal.show();
        terminal.sendText([python(), dev.rel, ...args].map(shellQuote).join(" "));
    }

    function productOf(arg) {
        const name = arg && (arg.product || (arg.id || "").replace(/^product:/, ""));
        return tree.info && tree.info.products.find((p) => p.name === name);
    }

    async function pickPreset(kind) {
        if (!tree.info) return;
        const groups = tree.info.presets;
        const own = (groups.user || []).filter((n) => (kind === "web") === n.startsWith("web-"));
        const items = [...(groups[kind] || []), ...own].map((name) => {
            const dev = tree.info.devPresets[name];
            return { label: name, description: dev ? Object.entries(dev.flags).map(([k, v]) => `${k}=${v}`).join(", ") : name === tree.info.defaults[kind] ? "default" : "" };
        });
        const pick = await vscode.window.showQuickPick(items, { placeHolder: `the ${kind} preset` });
        if (pick) await tree.pick(kind, pick.label);
    }

    async function toggleFlag(name) {
        const dev = findDev();
        const preset = tree.picked("desktop");
        const flag = tree.info && tree.info.flags.find((f) => f.name === name);
        if (!dev || !preset || !flag) return;
        const now = flagValue(tree.info, preset, flag);
        const next = now === "ON" ? "OFF" : "ON";
        const args = next === (flag.default || "OFF") ? ["--unset", name] : ["--set", `${name}=${next}`];
        try {
            const result = JSON.parse(await runDev(dev, ["flags", preset, ...args, "--json"]));
            await tree.pick("desktop", result.preset);
            vscode.window.setStatusBarMessage(`${result.preset}: ${Object.entries(result.flags).map(([k, v]) => `${k}=${v}`).join(", ") || "no flags"} (the first build configures its folder)`, 6000);
        } catch (error) {
            vscode.window.showErrorMessage(`dev.py flags: ${error.message}`);
        }
        await tree.refresh();
    }

    function productArgs(verb, product, extra) {
        const preset = tree.presetFor(product);
        return [verb, product.name, ...(preset ? ["--preset", preset] : []), ...(extra || [])];
    }

    context.subscriptions.push(
        vscode.commands.registerCommand("bagiDev.refresh", () => tree.refresh()),
        vscode.commands.registerCommand("bagiDev.pickPreset", (kind) => pickPreset(kind || "desktop")),
        vscode.commands.registerCommand("bagiDev.toggleFlag", toggleFlag),
        vscode.commands.registerCommand("bagiDev.build", (arg) => {
            const p = productOf(arg);
            if (p) inTerminal(`dev: build ${p.name}`, productArgs("build", p));
        }),
        vscode.commands.registerCommand("bagiDev.configure", (arg) => {
            const p = productOf(arg);
            if (p) inTerminal(`dev: build ${p.name}`, productArgs("build", p, ["--configure"]));
        }),
        vscode.commands.registerCommand("bagiDev.run", (arg) => {
            const p = productOf(arg);
            if (p) inTerminal(`dev: ${p.name}`, productArgs("run", p));
        }),
        vscode.commands.registerCommand("bagiDev.chain", (arg) => {
            const name = arg && (arg.chain || (arg.id || "").replace(/^chain:/, ""));
            if (!name) return;
            const desktop = tree.picked("desktop");
            const web = tree.picked("web");
            inTerminal(`dev: ${name}`, ["chain", name, ...(desktop ? ["--preset", desktop] : []), ...(web ? ["--web-preset", web] : [])]);
        }),
        vscode.commands.registerCommand("bagiDev.tool", (arg) => {
            const name = arg && (arg.tool || (arg.id || "").replace(/^tool:/, ""));
            const desktop = tree.picked("desktop");
            if (name) inTerminal(`dev: ${name}`, ["tool", name, ...(desktop ? ["--preset", desktop] : [])]);
        }),
        vscode.commands.registerCommand("bagiDev.openManifest", async () => {
            const dev = findDev();
            if (!dev) return;
            const file = path.join(dev.root, "dev.json");
            if (!fs.existsSync(file)) fs.writeFileSync(file, JSON.stringify({ products: [], chains: [], tools: [] }, null, 4) + "\n");
            await vscode.window.showTextDocument(vscode.Uri.file(file));
        }),
        vscode.workspace.onDidChangeWorkspaceFolders(() => tree.refresh())
    );

    // dev.json and CMakeUserPresets.json changed by hand (or by CMake Tools): read again.
    for (const pattern of ["dev.json", "CMakeUserPresets.json", "CMakePresets.json"]) {
        const watcher = vscode.workspace.createFileSystemWatcher(`**/${pattern}`);
        watcher.onDidChange(() => tree.refresh());
        watcher.onDidCreate(() => tree.refresh());
        watcher.onDidDelete(() => tree.refresh());
        context.subscriptions.push(watcher);
    }
    tree.refresh();
}

function deactivate() {}

module.exports = { activate, deactivate };
