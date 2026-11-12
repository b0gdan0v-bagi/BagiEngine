// Builds the panel runs: Verify (configure, build, ctest in the main checkout), Build & run of a branch in the worker
// that holds it, and Shots (tools/ui_shots.py on the branch and on the base, shown side by side).
"use strict";

const vscode = require("vscode");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { config, findTool, runTool, runGit, shellQuote: q, toolEnv } = require("./tools");

const VERIFY_KEY = "todoWorkers.lastVerify";
const SHOTS_ROOT = path.join(os.homedir(), ".claude", "plans", "review-shots"); // outside every work tree (ui_shots.py refuses one)

function attach(ctx) {
    let buildInfo = null; // worker.py build-info: preset, product, tests, exe, shots
    async function getBuildInfo() {
        const tool = findTool();
        if (!tool) return null;
        if (buildInfo && buildInfo.forPreset === config().preset) return buildInfo;
        const args = ["build-info", "--json", ...(config().preset ? ["--preset", config().preset] : [])];
        try {
            buildInfo = { ...JSON.parse(await runTool(tool, args, 60000)), forPreset: config().preset };
        } catch (error) {
            // remembered, so the refresh timer does not ask (and complain) every time; a change of the preset asks again
            vscode.window.showErrorMessage(`worker.py build-info: ${error.message}`);
            buildInfo = { forPreset: config().preset, error: error.message };
        }
        return buildInfo;
    }

    // A shell command as a VS Code task (its own terminal, the problem matchers of none); resolves to the exit code.
    function runTask(name, cwd, command) {
        return new Promise((resolve) => {
            const task = new vscode.Task({ type: "todoWorkers", name }, vscode.TaskScope.Workspace, name, "todo", new vscode.ShellExecution(command, { cwd, env: { PATH: toolEnv().PATH } }));
            task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: true };
            let execution = null;
            const done = vscode.tasks.onDidEndTaskProcess((event) => {
                if (execution && event.execution === execution) {
                    done.dispose();
                    resolve(event.exitCode === undefined ? -1 : event.exitCode);
                }
            });
            vscode.tasks.executeTask(task).then(
                (started) => (execution = started),
                (error) => {
                    done.dispose();
                    vscode.window.showErrorMessage(`${name}: ${error.message}`);
                    resolve(-1);
                }
            );
        });
    }

    function buildCommand(info, targets) {
        return `cmake --preset ${q(info.preset)} && cmake --build --preset ${q(info.preset)} --target ${targets.map(q).join(" ")}`;
    }

    // Verify: configure, build the product and the tests, ctest, in the main checkout (where a merge lands).
    let verifying = false;
    async function verifyMain(about) {
        const tool = findTool();
        const info = await getBuildInfo();
        if (!tool || !info || !info.preset || !info.product) {
            vscode.window.showWarningMessage("Verify: no preset or product (worker.py build-info; the Product field of .claude/rules/build-project.md)");
            return;
        }
        if (verifying) return;
        verifying = true;
        const label = about ? `after ${about}` : "of the main checkout";
        const head = (await runGit(tool.cwd, ["rev-parse", "--short", "HEAD"]).catch(() => "?")).trim();
        ctx.view.show({ ...ctx.view.payload, verifying: label });
        try {
            const code = await runTask(`verify ${label}`, tool.cwd, `${buildCommand(info, [info.product, ...info.tests])} && ctest --preset ${q(info.preset)} -j 8`);
            const result = { at: Date.now(), ok: code === 0, code, head, about: about || null, preset: info.preset };
            await ctx.context.globalState.update(VERIFY_KEY, result);
            ctx.addFeed(result.ok ? "verified" : "broken", `${result.ok ? "build and ctest green" : `build or ctest failed (exit ${code})`} ${label} at ${head}`, { entry: about || null });
            if (result.ok) vscode.window.showInformationMessage(`Verify ${label}: build and ctest green (${head})`);
            else vscode.window.showErrorMessage(`Verify ${label}: failed (exit ${code}, ${head}); the merge commit is local: fix it or reset it yourself`, "Show task").then((pick) => pick && vscode.commands.executeCommand("workbench.action.tasks.showTasks"));
        } finally {
            verifying = false;
            ctx.view.show({ ...ctx.view.payload, verifying: null, lastVerify: lastVerify() });
        }
    }

    function lastVerify() {
        return ctx.context.globalState.get(VERIFY_KEY, null);
    }

    // Build & run: the product built and started in the worker folder that holds the branch.
    function holderOf(entry) {
        return ctx.state && entry && ctx.state.workers.find((w) => [w.head, w.engine].some((h) => h && h.branch === entry.branch));
    }

    async function runBranch(entry, worker) {
        const info = await getBuildInfo();
        const where = worker || holderOf(entry);
        if (!info || !info.exe) {
            vscode.window.showWarningMessage("Build & run: no preset or product (worker.py build-info)");
            return;
        }
        if (!where) {
            vscode.window.showWarningMessage(`${entry ? entry.branch : "the branch"} is checked out in no worker folder: switch a free worker to it, or try it in the main checkout yourself`);
            return;
        }
        const terminal = vscode.window.createTerminal({ name: `run ${entry ? entry.id : where.name}`, cwd: where.path, env: { PATH: toolEnv().PATH } });
        terminal.show();
        terminal.sendText(`${buildCommand(info, [info.product])} && ./${info.exe}`);
        ctx.addFeed("run", `build & run of ${entry ? entry.id : where.name} in ${where.name}`, { entry: entry ? entry.id : null });
    }

    // Shots: the screenshot tool of the repository (tools/ui_shots.py) on the branch (its worker) and on the base
    // (the main checkout), into ~/.claude/plans/review-shots/<id>/{after,before}, then a tab that shows them side by side.
    async function shots(entry) {
        const tool = findTool();
        const info = await getBuildInfo();
        const where = holderOf(entry);
        if (!tool || !info || !info.shots || !info.exe) {
            vscode.window.showWarningMessage("Shots: this repository has no tools/ui_shots.py (or no product)");
            return;
        }
        if (!where) {
            vscode.window.showWarningMessage(`${entry.branch} is checked out in no worker folder: the after shots need it built somewhere`);
            return;
        }
        const out = path.join(SHOTS_ROOT, entry.id);
        const shoot = (cwd, label) => `(cd ${q(cwd)} && ${buildCommand(info, [info.product])} && python3 ${q(info.shots)} --exe ${q(info.exe)} --label ${label} --out ${q(out)})`;
        ctx.addFeed("shots", `screenshots of ${entry.id}: building the branch in ${where.name} and the base in the main checkout`, { entry: entry.id });
        const code = await runTask(`shots ${entry.id}`, tool.cwd, `rm -rf ${q(out)} && ${shoot(where.path, "after")} && ${shoot(tool.cwd, "before")}`);
        if (code !== 0) {
            vscode.window.showErrorMessage(`Shots of ${entry.id}: failed (exit ${code}); the task terminal says why`);
            return;
        }
        showShots(entry, out);
    }

    function showShots(entry, out) {
        const names = (dir) => {
            try {
                return fs.readdirSync(dir).filter((n) => n.endsWith(".png")).sort();
            } catch (error) {
                return [];
            }
        };
        const before = names(path.join(out, "before"));
        const after = names(path.join(out, "after"));
        const all = [...new Set([...before, ...after])].filter((n) => !n.includes("640x"));
        const panel = vscode.window.createWebviewPanel("todoWorkers.shots", `Shots: ${entry.id}`, vscode.ViewColumn.Active, { localResourceRoots: [vscode.Uri.file(out)] });
        const src = (side, name) => panel.webview.asWebviewUri(vscode.Uri.file(path.join(out, side, name))).toString();
        const esc = (t) => String(t).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
        const rows = all.map((name) => {
            const cell = (side, list) => (list.includes(name) ? `<img src="${src(side, name)}" alt="${side} ${esc(name)}">` : `<p class="none">no ${side} shot</p>`);
            return `<h3>${esc(name)}</h3><div class="pair"><figure>${cell("before", before)}<figcaption>before (${esc(entry.base)})</figcaption></figure><figure>${cell("after", after)}<figcaption>after (${esc(entry.branch)})</figcaption></figure></div>`;
        });
        panel.webview.html = `<!doctype html><html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src ${panel.webview.cspSource}; style-src 'unsafe-inline';">
<style>body{font-family:var(--vscode-font-family);color:var(--vscode-foreground);background:var(--vscode-editor-background);padding:8px 16px}
.pair{display:grid;grid-template-columns:1fr 1fr;gap:12px}figure{margin:0}img{width:100%;border:1px solid var(--vscode-widget-border,#444)}
figcaption,.none{opacity:.7;font-size:12px}h3{margin:18px 0 6px;font-size:13px}</style></head>
<body><h2>${esc(entry.id)}: ${esc(entry.title || "")}</h2>${rows.join("") || "<p>No screenshots were written.</p>"}</body></html>`;
        ctx.addFeed("shots", `screenshots of ${entry.id}: ${all.length} scenes before and after`, { entry: entry.id });
    }

    Object.assign(ctx, { getBuildInfo, verifyMain, lastVerify, isVerifying: () => verifying, runBranch, shots, cachedBuildInfo: () => buildInfo });
}

module.exports = { attach };
