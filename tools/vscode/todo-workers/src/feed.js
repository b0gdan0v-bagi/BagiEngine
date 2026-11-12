// What happened, kept across reloads (the loops' reports land here, not in a session), the notifications of what
// changed since the last read (review, waits, limit, ended) and the line in the status bar.
"use strict";

const vscode = require("vscode");
const { config, sessionWord } = require("./tools");

const FEED_KEY = "todoWorkers.feed";
const FEED_KEEP = 200;

function attach(ctx) {
    let previous = null; // {review: Set, active: Map, waiting: Set, limited: Set}

    function feed() {
        return ctx.context.globalState.get(FEED_KEY, []);
    }

    function addFeed(kind, text, extra) {
        const items = [{ at: Date.now(), kind, text, ...(extra || {}) }, ...feed()].slice(0, FEED_KEEP);
        ctx.context.globalState.update(FEED_KEY, items);
        if (ctx.state) ctx.view.show({ ...ctx.view.payload, feed: items });
    }

    function clearFeed() {
        return ctx.context.globalState.update(FEED_KEY, []).then(() => ctx.view.show({ ...ctx.view.payload, feed: [] }));
    }

    function notifyChanges() {
        const state = ctx.state;
        const now = {
            review: new Set(state.review.map((e) => e.id)),
            active: new Map(state.workers.map((w) => [w.name, w.active])),
            waiting: new Set(state.workers.filter((w) => w.waiting).map((w) => w.name)),
            limited: new Set(state.workers.filter((w) => w.limited).map((w) => w.name)),
        };
        if (previous) {
            for (const entry of state.review) {
                if (!previous.review.has(entry.id)) addFeed("review", `${entry.id} reached review: ${entry.statusNote || entry.branch}`, { entry: entry.id });
                if (!previous.review.has(entry.id) && config().notify) {
                    vscode.window.showInformationMessage(`Todo: ${entry.id} is in review (${entry.branch})`, "Review", "Review with Claude", "Merge + test", "Return with remarks").then((pick) => {
                        if (pick === "Review") ctx.reviewBranch(entry);
                        else if (pick === "Merge + test") ctx.quickMerge(entry, true);
                        else if (pick === "Review with Claude") ctx.startClaudeReview(entry);
                        else if (pick === "Return with remarks") ctx.returnWithRemarks(entry);
                    });
                }
            }
            for (const worker of state.workers) {
                // A loop that asks a question or a permission stops until someone answers: Answer opens it.
                const notify = config().notify;
                if (worker.waiting && !previous.waiting.has(worker.name)) addFeed("waiting", `${worker.name} waits for you: ${worker.asking ? `${worker.asking.tool}: ${worker.asking.text}` : "input needed"}`, { entry: worker.entry ? worker.entry.id : null });
                if (worker.limited && !previous.limited.has(worker.name)) addFeed("limited", `${worker.name} hit the usage limit${worker.limited.resets ? `, resets ${worker.limited.resets}` : ""}`);
                if (previous.active.get(worker.name) && !worker.active && !worker.limited && !worker.stopped) addFeed("ended", `${worker.name}: the loop ended${worker.entry ? ` (${worker.entry.id}, ${worker.entry.status})` : ""}`, { entry: worker.entry ? worker.entry.id : null });
                if (!notify) continue;
                if (worker.waiting && !previous.waiting.has(worker.name)) {
                    const what = worker.asking ? `${worker.asking.tool}: ${worker.asking.text}` : (worker.session && worker.session.waitingFor) || "input needed";
                    const about = worker.entry ? ` (${worker.entry.id})` : "";
                    vscode.window.showWarningMessage(`Todo: ${worker.name}${about} waits for you: ${what}`, "Answer").then((pick) => pick && ctx.openSessionTerminal(worker, "attach"));
                }
                // The usage limit stops a loop until it resets: Relaunch continues it then (the button stays on the card).
                if (worker.limited && !previous.limited.has(worker.name)) {
                    const when = worker.limited.resets ? `, resets ${worker.limited.resets}` : "";
                    vscode.window.showWarningMessage(`Todo: ${worker.name} was stopped by the usage limit${when}. Relaunch it after the reset.`, "Relaunch").then((pick) => pick && ctx.relaunch(ctx.workerOf(worker.k) || worker));
                }
                if (previous.active.get(worker.name) && !worker.active && !worker.limited && !worker.stopped) {
                    vscode.window.showInformationMessage(`Todo: the session of ${worker.name} has ended`, "Show log").then((pick) => pick && ctx.openSessionTerminal(worker, "logs"));
                }
            }
        }
        if (state.store === "ok" && state.sessionsKnown) previous = now;
    }

    function updateStatus(error) {
        const status = ctx.status;
        const state = ctx.state;
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

    Object.assign(ctx, { feed, addFeed, clearFeed, notifyChanges, updateStatus });
}

module.exports = { attach };
