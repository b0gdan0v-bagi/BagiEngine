// The entries in review: the diff, Review with Claude, Quick merge (worker.py merge) and Merge with Claude
// (/todo-merge), the entries a merge unblocked, and Return with remarks (worker.py remarks).
"use strict";

const vscode = require("vscode");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { config, findTool, runTool, runGit, shellQuote, terminalWith } = require("./tools");

function attach(ctx) {
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
    async function quickMerge(entry, verify) {
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
            if (verify) detail.push("Then the main checkout is configured, built and tested (ctest) in a task terminal; the result lands on the panel.");
            else if (plan.moved) detail.push(`The base moved since the branch was cut${entry.riskFiles && entry.riskFiles.length ? ` and changed ${entry.riskFiles.length} of its files` : ""}: the merged result is not built or tested here. Merge + test does that.`);
            detail.push(plan.reopen ? "The entry is a slice: it goes back to open for the next one." : "The entry is closed.");
            if (plan.heldBy) detail.push(`${plan.heldBy} lets go of the branch (detached at ${plan.base}); the branch is deleted.`);
            else detail.push("The branch is deleted.");
            if (plan.pointer) detail.push(`Then one commit in the game: "Update engine: ${plan.title}".`);
            detail.push("Nothing is pushed.");
            const pick = await vscode.window.showWarningMessage(`Merge ${plan.branch} into ${plan.base}${verify ? " and test it" : ""}?`, { modal: true, detail: detail.join("\n") }, verify ? "Merge + test" : "Merge");
            if (!pick) return;
            let merged = null;
            try {
                const result = JSON.parse(await runTool(tool, ["merge", entry.id, "--json"], 120000));
                const parts = [`Merged ${result.branch} into ${result.base} (${result.merge})`, `entry ${result.status || "still in review"}`];
                if (result.pointer) parts.push(`engine pointer ${result.pointer}`);
                const text = parts.join(", ");
                if (result.notes.length) vscode.window.showWarningMessage(`${text}. ${result.notes.join("; ")}`);
                else vscode.window.showInformationMessage(text);
                ctx.addFeed("merged", text, { entry: entry.id });
                merged = result;
            } catch (error) {
                vscode.window.showErrorMessage(`Quick merge of ${entry.id}: ${error.message}`);
            }
            if (merged && merged.unblocked && merged.unblocked.length) offerUnblocked(merged.unblocked, entry.id);
            if (merged && verify) ctx.verifyMain(entry.id);
        } finally {
            merging.delete(entry.id);
            ctx.refresh();
        }
    }

    // The entries a merge let go (their last open blocker was the merged one): start one in a free worker or queue it.
    function offerUnblocked(unblocked, by) {
        const ids = unblocked.map((u) => u.id);
        ctx.addFeed("unblocked", `${by} merged: ${ids.join(", ")} can start now`, { entry: by });
        const first = ids.slice(0, 2);
        vscode.window.showInformationMessage(`Todo: merging ${by} unblocked ${ids.join(", ")}`, ...first.map((id) => `Start ${id}`), ...first.map((id) => `Queue ${id}`)).then((pick) => {
            if (!pick) return;
            const id = pick.replace(/^(Start|Queue) /, "");
            if (pick.startsWith("Start")) ctx.startEntryInWorker(id);
            else ctx.queueEntry(id);
        });
    }

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

    // /todo-merge needs the owner's answer and the main checkout, so the button starts an interactive
    // Claude there with the skill; the skill reports, asks and merges. The extension itself writes nothing.
    function startMerge(entry) {
        const tool = findTool();
        if (!entry || !entry.branch || !tool || notHere(entry)) return;
        terminalWith(`todo-merge ${entry.id}`, tool.cwd, `${shellQuote(config().claude)} ${shellQuote(`/todo-merge ${entry.branch}`)}`);
    }

    // Review with Claude: an interactive claude in the main checkout that reads the entry and the diff of the
    // branch against its base and reports findings. Read only: it is told not to check out, edit, commit or merge.
    function startClaudeReview(entry) {
        const tool = findTool();
        if (!entry || !entry.branch || !entry.base || !tool || notHere(entry)) return;
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
        terminalWith(`todo-review ${entry.id}`, tool.cwd, `${shellQuote(config().claude)} ${shellQuote(prompt)}`);
    }

    // Return with remarks: the owner's remarks (a draft in ~/.claude/plans, edited in the editor) go to worker.py
    // remarks, which stores them, sets the entry back to in progress and starts the worker that holds the branch.
    const REMARKS_SAVE = "Send the remarks";
    async function returnWithRemarks(entry) {
        const tool = findTool();
        if (!entry || !tool || notHere(entry)) return;
        const draft = path.join(os.homedir(), ".claude", "plans", `remarks-draft-${entry.id}.md`);
        if (!fs.existsSync(draft)) {
            fs.mkdirSync(path.dirname(draft), { recursive: true });
            fs.writeFileSync(draft, `<!-- Remarks on ${entry.branch} (${entry.id}). Write what to change, one point per line; save, then press "${REMARKS_SAVE}". This line is dropped. -->\n\n- \n`);
        }
        await vscode.window.showTextDocument(vscode.Uri.file(draft), { preview: false });
        const pick = await vscode.window.showInformationMessage(
            `Remarks on ${entry.id}: write them in the open file, save it, then send. The loop fixes them on the same branch and asks for review again.`,
            REMARKS_SAVE,
            "Cancel"
        );
        if (pick !== REMARKS_SAVE) return;
        const doc = vscode.workspace.textDocuments.find((d) => d.uri.fsPath === draft);
        if (doc && doc.isDirty) await doc.save();
        const text = fs.readFileSync(draft, "utf8").replace(/<!--[\s\S]*?-->/g, "").trim();
        if (!text || text === "-") {
            vscode.window.showWarningMessage("The remarks are empty: nothing was sent");
            return;
        }
        const c = config();
        const args = ["remarks", entry.id, "--file", "-", "--start", "--json", "--claude", c.claude];
        if (c.reportTo) args.push("--report-to", c.reportTo);
        try {
            const result = JSON.parse(await runTool(tool, args, 120000, text));
            const where = result.worker ? `${result.worker}${result.started ? " started" : " (queued first; it starts when free)"}` : "no free worker: start it from the Todo list";
            ctx.addFeed("remarks", `${entry.id} returned with remarks, round ${result.round}: ${where}`, { entry: entry.id });
            vscode.window.showInformationMessage(`${entry.id}: remarks round ${result.round} sent, ${where}`);
            fs.unlinkSync(draft);
        } catch (error) {
            vscode.window.showErrorMessage(`worker.py remarks ${entry.id}: ${error.message}`);
        }
        ctx.refresh();
    }

    Object.assign(ctx, { reviewBranch, quickMerge, startMerge, startClaudeReview, returnWithRemarks });
}

module.exports = { attach };
