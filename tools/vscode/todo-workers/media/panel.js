// The webview of the Todo Workers panel. Everything shown comes from worker.py (and through it from
// session transcripts), so it is put into the page as text only (textContent), never as HTML.
(function () {
    const vscode = acquireVsCodeApi();
    const root = document.getElementById("root");
    let payload = { loading: true };
    const saved = vscode.getState() || {};
    const openSteps = new Set(saved.openSteps || []);
    // Worker cards are folded to a few lines by default; this set holds the ones the owner unfolded.
    const expanded = new Set(saved.expanded || []);
    const isFolded = (name) => !expanded.has(name);

    function save() {
        vscode.setState({ openSteps: [...openSteps], expanded: [...expanded], feedOpen: saved.feedOpen });
    }

    // The secondary buttons of a card behind "...": the main ones stay in sight.
    function more(buttons) {
        const box = el("details", "more");
        box.appendChild(el("summary", "", "\u2026"));
        const list = el("div", "more-list");
        buttons.filter(Boolean).forEach((b) => list.appendChild(b));
        box.appendChild(list);
        return box;
    }

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
        if (worker.limited) {
            word = "usage limit";
            kind = "limited";
        } else if (worker.stopped) {
            word = "stopped";
            kind = "stopped";
        } else if ((s && s.state === "done") || worker.endedTurn) {
            word = "done";
            kind = "done";
        } else if (worker.waiting) {
            word = "waits for you";
            kind = "waiting";
        } else if (s && s.status === "busy") {
            word = "busy";
            kind = "busy";
        } else if (s) {
            word = "idle";
            kind = "idle";
        }
        // A finished session can stay in `claude agents` for hours: its age is no run time, so it shows when
        // its last step was (the transcript), and a running one how long it has run.
        const ended = kind === "done" && worker.activity ? ago(worker.activity.updated) : "";
        const text = kind === "stopped" ? `stopped · ${ago(worker.stopped)}` : kind === "limited" ? (worker.limited.resets ? `usage limit · resets ${worker.limited.resets}` : "usage limit") : kind === "done" ? (ended ? `done · ended ${ended}` : "done") : s && s.startedAt ? `${word} · ${duration(s.startedAt)}` : word;
        const badge = el("span", `badge ${kind}`, text);
        if (kind === "idle") badge.title = "Idle: waiting for an agent of its own (a reviewer, a build)";
        if (kind === "waiting") badge.title = `The loop waits for your answer (${(s && s.waitingFor) || "input needed"}): Answer opens it`;
        if (kind === "limited") badge.title = `${worker.limited.text}. Relaunch continues the loop after the reset`;
        if (kind === "stopped") badge.title = "worker.py stop ended this session; its entry stays in progress on its branch: Relaunch or Fast start continues it";
        if (kind === "done") badge.title = worker.endedTurn ? "The run ended its turn (claude agents still lists it as blocked, but nothing waits): Stop clears it, Log shows it" : "The session has finished; Log shows it, Update and Fast start begin anew";
        return badge;
    }

    // The bar is the whole entry when it has several slice plans ("slice 3: 120/190 over 3 slices"), else its plan.
    function progress(entry) {
        const p = entry && entry.planProgress;
        const all = entry && entry.slices && entry.slices.count > 1 ? entry.slices : null;
        const shown = all || p;
        if (!shown || !shown.total) return null;
        const box = el("div", "progress");
        const bar = el("div", "bar");
        const fill = el("div", "fill");
        fill.style.width = `${Math.round((100 * shown.done) / shown.total)}%`;
        bar.appendChild(fill);
        const text = all ? `${all.count} slices ${all.done}/${all.total}${p && p.total ? ` · this slice ${p.done}/${p.total}` : ""}` : `plan ${p.done}/${p.total}`;
        box.append(bar, el("span", "muted", text));
        return box;
    }

    // Through (one branch, slices without a merge between) and the money against the entry's cap.
    function throughLine(entry) {
        if (!entry) return null;
        const line = el("div", "through-line");
        const on = entry.slicing === "through";
        const cost = entryCost(entry.id);
        const toggle = button(on ? "through" : "slice by slice", { cmd: "through", entry: entry.id, on: !on }, on
            ? "Through: the next slice starts on the same branch as soon as this one is approved; you merge once. Click to turn it off."
            : "Each slice waits for your merge. Click to turn on through (an optional cost cap is asked).", on ? "toggle on" : "toggle");
        line.appendChild(toggle);
        if (cost !== null || entry.costCap) {
            const over = entry.costCap && cost !== null && cost >= entry.costCap;
            line.appendChild(el("span", over ? "warn-text" : "muted", `${dollars(cost || 0)}${entry.costCap ? ` of $${entry.costCap}` : ""}`));
        }
        return line;
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

    // Dollars at the API list prices of the settings (todoWorkers.prices): the first key that is part of the model id.
    function priceOf(model) {
        const prices = payload.prices || {};
        const key = Object.keys(prices).find((k) => String(model || "").includes(k));
        return key ? prices[key] : null;
    }

    function costOf(models) {
        let sum = 0;
        let known = false;
        Object.entries(models || {}).forEach(([model, c]) => {
            const p = priceOf(model);
            if (!p) return;
            known = true;
            sum += ((c.input || 0) * p.input + (c.output || 0) * p.output + (c.cacheWrite || 0) * p.cacheWrite + (c.cacheRead || 0) * p.cacheRead) / 1e6;
        });
        return known ? sum : null;
    }

    function dollars(n) {
        return n === null || n === undefined ? "" : n >= 100 ? `$${Math.round(n)}` : `$${n.toFixed(2)}`;
    }

    function entryCost(id) {
        const usage = payload.state && payload.state.entryUsage;
        return usage && usage[id] ? costOf(usage[id]) : null;
    }

    // Tokens: the whole run, the last minutes ("now"), the context of the session, by model.
    function usageBlock(usage, withContext) {
        if (!usage || !usage.total || !usage.total.total) return null;
        const box = el("div", "usage");
        const top = el("div", "usage-top");
        top.append(el("span", "label", "Tokens"), el("span", "big", tokens(usage.total.total)));
        const cost = costOf(usage.models);
        if (cost !== null) {
            const chip = el("span", "big cost", dollars(cost));
            chip.title = "what these tokens cost at the API list prices (todoWorkers.prices); a subscription bills otherwise";
            top.append(chip);
        }
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
        const folded = isFolded(worker.name);
        const card = el("section", (worker.waiting ? "card waiting" : worker.limited ? "card limited" : "card") + (folded ? " collapsed" : ""));
        const top = el("div", "card-top");
        const fold = el("button", "fold", folded ? "\u25B8" : "\u25BE");
        fold.title = folded ? "Expand: every detail of the worker" : "Collapse: only the status lines";
        fold.addEventListener("click", () => {
            folded ? expanded.add(worker.name) : expanded.delete(worker.name);
            save();
            render();
        });
        top.append(fold, el("span", "name", worker.name), el("span", "entry", entry ? entry.id : "no entry"));
        if (entry) top.append(el("span", `area ${entry.area}`, entry.area));
        top.append(sessionBadge(worker));
        card.appendChild(top);

        if (entry) card.appendChild(el("div", "title", entry.title));
        if (worker.queue && worker.queue.length) {
            const row = el("div", "queue");
            row.appendChild(el("span", "muted", "next:"));
            worker.queue.forEach((id) => {
                const chip = el("span", "chip", id);
                const x = el("button", "chip-x", "\u00D7");
                x.title = `take ${id} out of the queue of ${worker.name}`;
                x.addEventListener("click", () => vscode.postMessage({ cmd: "unqueue", entry: id, worker: worker.k }));
                chip.appendChild(x);
                row.appendChild(chip);
            });
            card.appendChild(row);
        }
        if (worker.waiting) {
            const ask = el("div", "ask");
            ask.append(
                el("span", "", worker.asking ? `Asks: ${worker.asking.tool}: ${worker.asking.text}` : `Waits for your answer: ${(worker.session && worker.session.waitingFor) || "input needed"}`),
                button("Answer", { cmd: "attach", worker: worker.k }, "claude attach: open the session and answer its question or permission", "primary")
            );
            card.appendChild(ask);
        }
        if (worker.limited) {
            const ask = el("div", "ask limit");
            ask.append(
                el("span", "", `Stopped by the usage limit${worker.limited.resets ? `; resets ${worker.limited.resets}` : ""}`),
                button("Relaunch", { cmd: "relaunch", worker: worker.k }, "worker.py relaunch <k>: claude --bg --resume of this session with \"go on where the loop stopped\" (press it after the reset)", "primary")
            );
            card.appendChild(ask);
        }
        if (folded) {
            // a few lines: the top, the title, the review note or the plan, what the loop said last
            if (entry && entry.status === "review") card.appendChild(el("div", "note ok", "in review" + (entry.statusNote ? `: ${entry.statusNote}` : "")));
            else if (entry) {
                const bar = progress(entry);
                if (bar) card.appendChild(bar);
            }
            const events = (worker.activity && worker.activity.events) || [];
            const said = [...events].reverse().find((e) => e.kind === "say");
            if (said) {
                const now = el("div", "now one-line");
                now.append(el("span", "label", worker.active && !worker.endedTurn ? "Now" : "Last"), el("span", "time", ago(said.time)), el("span", "said", plain(said.text)));
                card.appendChild(now);
            }
            const main = mainAction(worker);
            if (main) {
                const actions = el("div", "actions");
                actions.appendChild(main);
                card.appendChild(actions);
            }
            return card;
        }
        if (entry) {
            if (entry.status === "review") card.appendChild(el("div", "note ok", "in review" + (entry.statusNote ? `: ${entry.statusNote}` : "")));
            else if (entry.statusNote) card.appendChild(el("div", "note", entry.statusNote));
            const bar = progress(entry);
            if (bar) card.appendChild(bar);
            const through = throughLine(entry);
            if (through) card.appendChild(through);
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
                now.append(el("span", "label", worker.active && !worker.endedTurn ? "Now" : "Last"), el("span", "time", ago(said.time)), el("div", "said", plain(said.text)));
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
                    save();
                });
                details.appendChild(el("summary", "", `Last steps (${events.length}) · updated ${ago(activity.updated)}`));
                [...events].reverse().forEach((e) => details.appendChild(step(e)));
                card.appendChild(details);
            }
        } else if (worker.session) {
            card.appendChild(el("div", "muted", "No transcript found for this session."));
        }

        const actions = el("div", "actions");
        if (worker.session && !worker.waiting) actions.append(button("Attach", { cmd: "attach", worker: worker.k }, "claude attach: open the session"));
        if (worker.session && !worker.limited && worker.session.status !== "busy" && entry && entry.status !== "review")
            actions.append(button("Relaunch", { cmd: "relaunch", worker: worker.k }, "worker.py relaunch <k>: continue this session's conversation in the background (a loop that ended before its entry reached review)"));
        if (worker.active) actions.append(button("Stop", { cmd: "stop", worker: worker.k }, "worker.py stop", true));
        else actions.append(button("Fast start", { cmd: "fastStart", worker: worker.k }, "worker.py start <k> now: /todo-loop with the slices, loop arguments and report session of the settings (todoWorkers.*)", "primary"));
        actions.append(more([
            worker.session && button("Log", { cmd: "logs", worker: worker.k }, "claude logs"),
            entry && button("Plan", { cmd: "plan", entry: entry.id }, "the plan the entry shows (its newest slice plan)"),
            entry && entry.branch && button("Build & run", { cmd: "run", worker: worker.k }, "configure, build the product and start it in this worker folder (its branch as it is now)"),
            !worker.active && button("Update", { cmd: "update", worker: worker.k }, "worker.py update: detach the game and engine/ at the tips of their base branches (main, games/...); the branch it held is kept; refused with uncommitted changes"),
            button("Folder", { cmd: "folder", worker: worker.k }, "open the worker in a new window"),
            button("Terminal", { cmd: "terminal", worker: worker.k }),
        ]));
        card.appendChild(actions);
        return card;
    }

    // The one action a folded card offers: what the worker needs from the owner, else what it can do next.
    function mainAction(worker) {
        if (worker.waiting || worker.limited) return null; // their own lines carry Answer and Relaunch
        const entry = worker.entry;
        if (entry && entry.status === "review") return button("Review", { cmd: "review", entry: entry.id }, "the changes of the branch against its base, in one diff editor", "primary");
        if (worker.active) return null;
        return button("Fast start", { cmd: "fastStart", worker: worker.k }, "worker.py start <k> now", "primary");
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
        const bar = progress(entry);
        if (bar) card.appendChild(bar);
        const through = throughLine(entry);
        if (through) card.appendChild(through);
        const first = (entry.blockedBy || []).filter((b) => b.status !== "closed").map((b) => b.id);
        if (first.length) card.appendChild(el("div", "warn soft", `merge first: ${first.join(", ")} (it waits for them; worker.py merge refuses it until they are closed)`));
        // worker.py list --store: what a merge meets (review_checks)
        if (entry.conflicts && entry.conflicts.length) card.appendChild(el("div", "warn", `conflicts with ${entry.base}: ${entry.conflicts.join(", ")}`));
        (entry.conflictsWith || []).forEach((c) => card.appendChild(el("div", "warn", `conflicts with ${c.id} (also in review): ${c.files.join(", ")}; the second to merge needs a hand`)));
        if (entry.behind && entry.behind.count) {
            const line = el("div", entry.riskFiles && entry.riskFiles.length ? "warn soft" : "muted", `${entry.base} moved ${entry.behind.count} commit${entry.behind.count === 1 ? "" : "s"} since the cut` + (entry.riskFiles && entry.riskFiles.length ? `; they changed ${entry.riskFiles.length} of its files: ${entry.riskFiles.slice(0, 6).join(", ")}${entry.riskFiles.length > 6 ? ", ..." : ""} (no git conflict, but test the merge)` : ""));
            line.title = entry.behind.subjects.join("\n");
            card.appendChild(line);
        }
        const opens = (entry.blocks || []).filter((b) => b.status !== "closed").map((b) => b.id);
        if (opens.length) card.appendChild(el("div", "muted", `waiting on it: ${opens.join(", ")}`));
        // the claim stored where the branch was made (todo.py claim --base: takenBase, takenOrigin); older entries have neither
        const taken = [entry.takenOrigin ? `taken in ${entry.takenOrigin}` : "", entry.takenBase ? `base ${entry.takenBase}` : ""].filter(Boolean).join(", ");
        if (taken) card.appendChild(el("div", "muted", taken));
        // worker.py list --store: the branch is not here, or it was cut from another base (another game's)
        const away = entry.local === false || Boolean(entry.foreignBase);
        if (away) {
            card.classList.add("away");
            let warning;
            if (entry.foreignBase && entry.takenBase) {
                warning = `Taken in ${entry.takenOrigin || "another checkout"}, base ${entry.takenBase}, not ${entry.base}: review and merge it in the checkout that made it.`;
            } else if (entry.foreignBase) {
                warning = `Cut from ${entry.foreignBase}, not from ${entry.base}: it holds commits that ${entry.base} has not. Review and merge it in the checkout that made it.`;
            } else if (entry.takenOrigin) {
                warning = `No branch ${entry.branch} here: it was taken in ${entry.takenOrigin}. Review and merge it there.`;
            } else {
                warning = `No branch ${entry.branch} here: it was taken in another checkout or on another machine. Review and merge it there.`;
            }
            card.appendChild(el("div", "warn", warning));
        }
        const actions = el("div", "actions");
        const main = [
            button("Review", { cmd: "review", entry: entry.id }, "the changes of the branch against its base, in one diff editor"),
            button("Merge + test", { cmd: "mergeTest", entry: entry.id }, "Quick merge, then configure, build and ctest the main checkout in a task terminal; the result lands on the panel", away || (entry.conflicts && entry.conflicts.length) || first.length ? "" : "primary"),
            button("Return with remarks", { cmd: "remarks", entry: entry.id }, "write what to change; the worker that holds the branch fixes it on the same branch, gets it reviewed and brings it back to review"),
        ];
        const rest = [
            button("Review with Claude", { cmd: "claudeReview", entry: entry.id }, "a terminal in the main checkout runs claude with a read-only review of the branch against its base: findings with file:line and a verdict"),
            button("Quick merge", { cmd: "quickMerge", entry: entry.id }, "worker.py merge: a dialog with the commits, then merge --no-ff, close the entry, delete the branch (and the engine pointer); no build"),
            button("Build & run", { cmd: "run", entry: entry.id }, "configure, build and start the product in the worker folder that holds the branch"),
            button("Merge with Claude", { cmd: "merge", entry: entry.id }, "a terminal in the main checkout runs claude '/todo-merge <branch>': it reports, asks, and builds when the base moved"),
            payload.shotsTool ? button("Shots", { cmd: "shots", entry: entry.id }, "screenshots of the branch (its worker) and of the base (the main checkout) with the repository's tools/ui_shots.py, side by side in a tab") : null,
        ].filter(Boolean);
        if (away) [...main, ...rest].forEach((b) => (b.disabled = true));
        actions.append(...main, more([...rest, button("Plan", { cmd: "plan", entry: entry.id })]));
        card.appendChild(actions);
        return card;
    }

    function render() {
        root.replaceChildren();
        const bar = el("div", "toolbar");
        const info = el("span", "muted", payload.refreshing ? "refreshing ..." : payload.at ? `updated ${ago(payload.at)}` : "");
        const limited = payload.state ? payload.state.workers.filter((w) => w.limited).length : 0;
        if (limited) bar.append(button(`Relaunch all (${limited})`, { cmd: "relaunchAll" }, "worker.py relaunch: continue every session the usage limit stopped (press it after the reset)", "primary"));
        const names = payload.state ? payload.state.workers.map((w) => w.name) : [];
        if (names.length) {
            const all = names.every((n) => isFolded(n));
            const toggle = el("button", "", all ? "Expand all" : "Collapse all");
            toggle.title = all ? "every detail of every worker" : "every worker card folded to its status lines";
            toggle.addEventListener("click", () => {
                names.forEach((n) => (all ? expanded.add(n) : expanded.delete(n)));
                save();
                render();
            });
            bar.append(toggle);
        }
        bar.append(info, button("Todo list", { cmd: "todoList" }, "the entries of the todo store in a tab of the editor"), button("Refresh", { cmd: "refresh" }), button("Settings", { cmd: "settings" }));
        root.appendChild(bar);

        if (payload.error) root.appendChild(el("div", "error", payload.error));
        const state = payload.state;
        if (!state) {
            if (payload.loading) root.appendChild(el("p", "muted", "Asking worker.py ..."));
            return;
        }
        if (!state.sessionsKnown) root.appendChild(el("div", "warn", "claude agents did not answer: set todoWorkers.claude to the full path of claude."));
        if (state.store !== "ok" && state.store !== "off") root.appendChild(el("div", "warn", `todo store: ${state.store}`));

        const p = state.pointer;
        if (p) {
            const line = el("div", p.moved ? "warn soft pointer" : "muted pointer");
            const text = p.moved
                ? `engine/ at ${p.checkout}${p.branch ? ` (${p.branch})` : ""}, the game records ${p.recorded}: the pointer is not committed`
                : `engine/ pointer ${p.recorded}${p.ahead && p.ahead.count ? `; ${p.base} is ${p.ahead.count} commit${p.ahead.count === 1 ? "" : "s"} ahead` : `, ${p.base || "its base"} has nothing newer`}`;
            line.appendChild(el("span", "", text));
            if (p.ahead && p.ahead.count) line.title = p.ahead.subjects.join("\n");
            if (p.moved) line.appendChild(button("Commit pointer", { cmd: "commitPointer" }, "one commit of engine/ only in the game: Update engine: <subject>"));
            root.appendChild(line);
        }
        const verify = payload.lastVerify;
        const check = el("div", verify && !verify.ok ? "warn verify" : "muted verify");
        check.appendChild(el("span", "", payload.verifying ? "verify runs ..." : verify ? `last verify ${verify.ok ? "green" : "RED"}${verify.about ? ` after ${verify.about}` : ""} at ${verify.head} · ${ago(verify.at)}` : "no verify yet"));
        check.appendChild(button("Verify", { cmd: "verify" }, "configure, build and ctest the main checkout now"));
        root.appendChild(check);

        const all = usageBlock(state.usage, false);
        if (all) {
            root.appendChild(el("h2", "", "All workers"));
            all.classList && all.classList.add("summary");
            root.appendChild(all);
        }

        root.appendChild(el("h2", "", `Workers (${state.workers.length})`));
        if (!state.workers.length) root.appendChild(el("p", "muted", "No worker folders. Make them with: python3 engine/tools/todo/worker.py create 3"));
        const rank = (w) => (w.waiting ? 0 : w.limited ? 1 : 2);
        [...state.workers].sort((a, b) => rank(a) - rank(b) || a.k - b.k).forEach((w) => root.appendChild(workerCard(w)));

        root.appendChild(el("h2", "", `Review: ${state.review.length} to merge`));
        if (!state.review.length) root.appendChild(el("p", "muted", "Nothing waits for /todo-merge."));
        state.review.forEach((e) => root.appendChild(reviewCard(e, state)));

        const items = payload.feed || [];
        const box = el("details", "feed");
        box.open = saved.feedOpen !== false;
        box.addEventListener("toggle", () => {
            saved.feedOpen = box.open;
            vscode.setState({ ...saved, openSteps: [...openSteps], expanded: [...expanded] });
        });
        box.appendChild(el("summary", "", `Feed (${items.length})`));
        items.slice(0, 40).forEach((item) => {
            const line = el("div", `feed-item ${item.kind}`);
            line.append(el("span", "time", ago(item.at)), el("span", "kind", item.kind), el("span", "text", item.text));
            box.appendChild(line);
        });
        if (items.length) box.appendChild(button("Clear", { cmd: "clearFeed" }, "forget the feed"));
        root.appendChild(el("h2", "", "What happened"));
        root.appendChild(box);
    }

    window.addEventListener("message", (event) => {
        payload = event.data || {};
        render();
    });
    setInterval(render, 15000); // the "... ago" texts
    render();
})();
