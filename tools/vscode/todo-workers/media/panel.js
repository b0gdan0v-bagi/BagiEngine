// The webview of the Todo Workers panel. Everything shown comes from worker.py (and through it from
// session transcripts), so it is put into the page as text only (textContent), never as HTML.
(function () {
    const vscode = acquireVsCodeApi();
    const root = document.getElementById("root");
    let payload = { loading: true };
    const saved = vscode.getState() || {};
    const openSteps = new Set(saved.openSteps || []);

    function el(tag, className, text) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined && text !== null) node.textContent = String(text);
        return node;
    }

    function ago(value) {
        const ms = typeof value === "number" ? value : Date.parse(value || "");
        if (!Number.isFinite(ms)) return "";
        const s = Math.max(0, Math.round((Date.now() - ms) / 1000));
        if (s < 60) return `${s} s ago`;
        const m = Math.floor(s / 60);
        if (m < 60) return `${m} min ago`;
        return `${Math.floor(m / 60)} h ${m % 60} min ago`;
    }

    function duration(startedAt) {
        if (typeof startedAt !== "number") return "";
        const m = Math.max(0, Math.floor((Date.now() - startedAt) / 60000));
        return m >= 60 ? `${Math.floor(m / 60)} h ${m % 60} min` : `${m} min`;
    }

    // kind: true or "danger" (a red label), "primary" (the main action of a card)
    function button(label, message, title, kind) {
        const b = el("button", kind === true ? "danger" : kind || "", label);
        if (title) b.title = title;
        b.addEventListener("click", () => vscode.postMessage(message));
        return b;
    }

    function sessionBadge(worker) {
        const s = worker.session;
        let word = "no session";
        let kind = "none";
        if (s && s.state === "done") {
            word = "done";
            kind = "done";
        } else if (s && s.status === "busy") {
            word = "busy";
            kind = "busy";
        } else if (s) {
            word = "idle";
            kind = "idle";
        }
        const badge = el("span", `badge ${kind}`, s && s.startedAt ? `${word} · ${duration(s.startedAt)}` : word);
        if (kind === "idle") badge.title = "Waiting: for an agent of its own, or for you (Attach to see)";
        return badge;
    }

    function progress(entry) {
        const p = entry && entry.planProgress;
        if (!p || !p.total) return null;
        const box = el("div", "progress");
        const bar = el("div", "bar");
        const fill = el("div", "fill");
        fill.style.width = `${Math.round((100 * p.done) / p.total)}%`;
        bar.appendChild(fill);
        box.append(bar, el("span", "muted", `plan ${p.done}/${p.total}`));
        return box;
    }

    function head(label, h) {
        if (!h) return null;
        const line = el("div", "head");
        line.append(el("span", "muted", `${label} `), el("code", "", h.branch || `detached ${h.commit}`));
        return line;
    }

    // Markdown links of the chat ("[Plan: ...](file:///...)") read better as their text.
    function plain(text) {
        return String(text || "").replace(/\[([^\]]+)\]\([^)\s]+\)/g, "$1");
    }

    function step(event) {
        const line = el("div", `step ${event.kind}`);
        line.append(el("span", "time", ago(event.time)));
        if (event.kind === "tool") line.append(el("span", "tool", event.tool));
        line.append(el("span", "text", plain(event.text)));
        return line;
    }

    function agentLine(agent) {
        const line = el("div", `agent ${agent.done ? "finished" : "running"}`);
        const top = el("div", "agent-top");
        top.append(
            el("span", "dot", agent.done ? "✓" : "●"),
            el("span", "agent-type", (agent.type || "agent").replace(/^todo-/, "")),
            el("span", "agent-desc", agent.description || ""),
            el("span", "time", ago(agent.updated))
        );
        line.appendChild(top);
        const last = agent.last && agent.last[agent.last.length - 1];
        if (last) line.appendChild(el("div", "agent-last", last.kind === "tool" && !agent.done ? `${last.tool}: ${last.text}` : plain(last.text)));
        return line;
    }

    function tokens(n) {
        n = n || 0;
        if (n >= 1e6) return `${(n / 1e6).toFixed(n >= 1e7 ? 0 : 1)}M`;
        if (n >= 1e3) return `${Math.round(n / 1e3)}k`;
        return String(n);
    }

    function shortModel(model) {
        return String(model || "?").replace(/^claude-/, "");
    }

    // Tokens: the whole run, the last minutes ("now"), the context of the session, by model.
    function usageBlock(usage, withContext) {
        if (!usage || !usage.total || !usage.total.total) return null;
        const box = el("div", "usage");
        const top = el("div", "usage-top");
        top.append(el("span", "label", "Tokens"), el("span", "big", tokens(usage.total.total)));
        const recent = usage.recent && usage.recent.total;
        top.append(el("span", `rate${recent ? "" : " muted"}`, `${tokens(recent)} in the last ${usage.recentMinutes} min`));
        box.appendChild(top);
        const t = usage.total;
        const split = el("div", "usage-split muted", `out ${tokens(t.output)} · in ${tokens(t.input)} · cache write ${tokens(t.cacheWrite)} · cache read ${tokens(t.cacheRead)}`);
        split.title = "Cache reads are what the sessions re-read of their context on every answer; they cost a fraction of other input.";
        box.appendChild(split);
        if (withContext) {
            const parts = [];
            if (usage.session && usage.agents) parts.push(`loop ${tokens(usage.session.total)} · agents ${tokens(usage.agents.total)}`);
            if (usage.context) parts.push(`context now ${tokens(usage.context)} (${shortModel(usage.contextModel)})`);
            if (parts.length) box.appendChild(el("div", "usage-split muted", parts.join(" · ")));
            const models = Object.entries(usage.models || {}).sort((a, b) => b[1].total - a[1].total);
            if (models.length) {
                const row = el("div", "models");
                const sum = usage.total.total || 1;
                models.forEach(([model, counts]) => {
                    const chip = el("span", "model", `${shortModel(model)} ${tokens(counts.total)}`);
                    chip.title = `${model}: ${Math.round((100 * counts.total) / sum)}% · out ${tokens(counts.output)}`;
                    row.appendChild(chip);
                });
                box.appendChild(row);
            }
        }
        return box;
    }

    function workerCard(worker) {
        const entry = worker.entry;
        const card = el("section", "card");
        const top = el("div", "card-top");
        top.append(el("span", "name", worker.name), el("span", "entry", entry ? entry.id : "no entry"));
        if (entry) top.append(el("span", `area ${entry.area}`, entry.area));
        top.append(sessionBadge(worker));
        card.appendChild(top);

        if (entry) {
            card.appendChild(el("div", "title", entry.title));
            if (entry.status === "review") card.appendChild(el("div", "note ok", "in review" + (entry.statusNote ? `: ${entry.statusNote}` : "")));
            const bar = progress(entry);
            if (bar) card.appendChild(bar);
        }
        const heads = el("div", "heads");
        [head("game", worker.head), head("engine", worker.engine)].forEach((h) => h && heads.appendChild(h));
        if (worker.dirty) heads.appendChild(el("div", "warn", "uncommitted changes (a developer is at work)"));
        card.appendChild(heads);

        const usage = usageBlock(worker.usage, true);
        if (usage) card.appendChild(usage);

        const activity = worker.activity;
        if (activity) {
            const events = activity.events || [];
            const said = [...events].reverse().find((e) => e.kind === "say");
            if (said) {
                const now = el("div", "now");
                now.append(el("span", "label", "Now"), el("span", "time", ago(said.time)), el("div", "said", plain(said.text)));
                card.appendChild(now);
            }
            const agents = activity.agents || [];
            if (agents.length) {
                const list = el("div", "agents");
                agents.forEach((a) => list.appendChild(agentLine(a)));
                card.appendChild(list);
            }
            if (events.length) {
                const details = el("details", "steps");
                details.open = openSteps.has(worker.name);
                details.addEventListener("toggle", () => {
                    details.open ? openSteps.add(worker.name) : openSteps.delete(worker.name);
                    vscode.setState({ openSteps: [...openSteps] });
                });
                details.appendChild(el("summary", "", `Last steps (${events.length}) · updated ${ago(activity.updated)}`));
                [...events].reverse().forEach((e) => details.appendChild(step(e)));
                card.appendChild(details);
            }
        } else if (worker.session) {
            card.appendChild(el("div", "muted", "No transcript found for this session."));
        }

        const actions = el("div", "actions");
        if (worker.session) {
            actions.append(button("Attach", { cmd: "attach", worker: worker.k }, "claude attach: answer a session that waits"), button("Log", { cmd: "logs", worker: worker.k }, "claude logs"));
        }
        actions.append(button("Folder", { cmd: "folder", worker: worker.k }, "open the worker in a new window"), button("Terminal", { cmd: "terminal", worker: worker.k }));
        if (entry) actions.append(button("Plan", { cmd: "plan", entry: entry.id }, "~/.claude/plans/todo-loop-<branch>.md"));
        if (worker.active) actions.append(button("Stop", { cmd: "stop", worker: worker.k }, "worker.py stop", true));
        card.appendChild(actions);
        return card;
    }

    function reviewCard(entry, state) {
        const card = el("section", "card review");
        const top = el("div", "card-top");
        top.append(el("span", "entry", entry.id), el("span", `area ${entry.area}`, entry.area));
        const where = state.workers.find((w) => [w.head, w.engine].some((h) => h && h.branch === entry.branch));
        if (where) top.append(el("span", "muted", `in ${where.name}`));
        card.append(top, el("div", "title", entry.title));
        if (entry.statusNote) card.appendChild(el("div", "note", entry.statusNote));
        card.appendChild(head("branch", { branch: entry.branch }));
        const actions = el("div", "actions");
        actions.append(
            button("Review", { cmd: "review", entry: entry.id }, "the changes of the branch against its base, in one diff editor"),
            button("Quick merge", { cmd: "quickMerge", entry: entry.id }, "worker.py merge: a dialog with the commits, then merge --no-ff, close the entry, delete the branch (and the engine pointer); no build", "primary"),
            button("Merge with Claude", { cmd: "merge", entry: entry.id }, "a terminal in the main checkout runs claude '/todo-merge <branch>': it reports, asks, and builds when the base moved"),
            button("Plan", { cmd: "plan", entry: entry.id })
        );
        card.appendChild(actions);
        return card;
    }

    function render() {
        root.replaceChildren();
        const bar = el("div", "toolbar");
        const info = el("span", "muted", payload.refreshing ? "refreshing ..." : payload.at ? `updated ${ago(payload.at)}` : "");
        bar.append(info, button("Refresh", { cmd: "refresh" }), button("Settings", { cmd: "settings" }));
        root.appendChild(bar);

        if (payload.error) root.appendChild(el("div", "error", payload.error));
        const state = payload.state;
        if (!state) {
            if (payload.loading) root.appendChild(el("p", "muted", "Asking worker.py ..."));
            return;
        }
        if (!state.sessionsKnown) root.appendChild(el("div", "warn", "claude agents did not answer: set todoWorkers.claude to the full path of claude."));
        if (state.store !== "ok" && state.store !== "off") root.appendChild(el("div", "warn", `todo store: ${state.store}`));

        const all = usageBlock(state.usage, false);
        if (all) {
            root.appendChild(el("h2", "", "All workers"));
            all.classList && all.classList.add("summary");
            root.appendChild(all);
        }

        root.appendChild(el("h2", "", `Workers (${state.workers.length})`));
        if (!state.workers.length) root.appendChild(el("p", "muted", "No worker folders. Make them with: python3 engine/tools/todo/worker.py create 3"));
        state.workers.forEach((w) => root.appendChild(workerCard(w)));

        root.appendChild(el("h2", "", `Review: ${state.review.length} to merge`));
        if (!state.review.length) root.appendChild(el("p", "muted", "Nothing waits for /todo-merge."));
        state.review.forEach((e) => root.appendChild(reviewCard(e, state)));
    }

    window.addEventListener("message", (event) => {
        payload = event.data || {};
        render();
    });
    setInterval(render, 15000); // the "... ago" texts
    render();
})();
