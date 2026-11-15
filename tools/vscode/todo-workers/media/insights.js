// The parts of the Workers panel built from worker.py's slower views and from the
// doctor and the diff of `list`: the problem lines and the diff of a worker card, the review rounds of a review card,
// and the sections Day, Timeline and Busy files. Everything is put in as text (textContent); the timeline is inline SVG.
// panel.js calls TodoInsights.* with `post` (vscode.postMessage).
(function () {
    const SVG = "http://www.w3.org/2000/svg";

    function el(tag, className, text) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined && text !== null) node.textContent = String(text);
        return node;
    }

    function svg(tag, attrs, text) {
        const node = document.createElementNS(SVG, tag);
        for (const [k, v] of Object.entries(attrs || {})) node.setAttribute(k, String(v));
        if (text !== undefined) node.textContent = String(text);
        return node;
    }

    function button(post, label, message, title, kind) {
        const b = el("button", kind || "", label);
        if (title) b.title = title;
        b.addEventListener("click", () => post(message));
        return b;
    }

    function clock(ms) {
        const d = new Date(ms);
        return `${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`;
    }

    // The doctor's lines of a worker (worker.py list: problems), each with Fix when worker.py can repair it.
    function problemLines(worker, post) {
        return (worker.problems || []).map((p) => {
            const line = el("div", "doctor");
            line.appendChild(el("span", "", p.text));
            if (p.fix) line.appendChild(button(post, "Fix", { cmd: "doctorFix", worker: worker.k, kind: p.kind }, `worker.py doctor ${worker.k} --fix ${p.kind}: ${p.fix}`));
            return line;
        });
    }

    // What the branch of the entry in progress holds so far (worker.py list --store: diff).
    function diffLine(worker) {
        const d = worker.diff;
        if (!d || (!d.commits && !d.uncommitted)) return null;
        const parts = [];
        if (d.commits) parts.push(`${d.commits} commit${d.commits === 1 ? "" : "s"}: ${d.committed || "no file changes"}`);
        if (d.uncommitted) parts.push(`not committed: ${d.uncommitted}`);
        return el("div", "muted diff-line", parts.join(" · "));
    }

    // The review rounds of an entry (worker.py review-stats): the reviewer's CHANGES and the owner's remarks.
    function reviewLine(entry, insights) {
        const stats = insights && insights.reviews && insights.reviews.entries && insights.reviews.entries[entry.id];
        if (!stats || !stats.reviews) return null;
        const parts = [`${stats.reviews} review${stats.reviews === 1 ? "" : "s"}`];
        parts.push(stats.changes ? `sent back ${stats.changes} time${stats.changes === 1 ? "" : "s"} by the reviewer` : "each approved at once");
        if (stats.remarks) parts.push(`${stats.remarks} remarks round${stats.remarks === 1 ? "" : "s"}`);
        const line = el("div", stats.changes + stats.remarks >= 3 ? "warn soft" : "muted", parts.join(" · "));
        line.title = "From the names of the review plans in the store: -r2 is a second round of the same review";
        return line;
    }

    function age(ms) {
        const m = Math.max(0, Math.round((Date.now() - ms) / 60000));
        return m < 1 ? "just now" : m < 60 ? `${m} min ago` : `${Math.floor(m / 60)} h ${m % 60} min ago`;
    }

    // `part`: the name of the view in insights (src/insights.js): its age, and "reading" while a newer answer comes
    function section(title, key, saved, onToggle, build, insights, part) {
        const box = el("details", "feed insight");
        box.open = Boolean(saved[key]);
        box.addEventListener("toggle", () => onToggle(key, box.open));
        const busy = insights && (insights.reading || []).includes(part);
        box.appendChild(el("summary", "", busy && insights[part] ? `${title} · reading …` : title));
        const read = insights && insights.read && insights.read[part];
        if (read && insights[part]) box.appendChild(el("div", "muted insight-age", `read ${age(read)}`));
        build(box);
        return box;
    }

    function failed(box, part) {
        if (part && part.error) {
            box.appendChild(el("div", "warn", part.error));
            return true;
        }
        if (!part) {
            box.appendChild(el("div", "muted", "reading ..."));
            return true;
        }
        return false;
    }

    // Day: what was merged since the start of the day, what waits, what is not pushed; End the day.
    function daySection(insights, state, saved, onToggle, post) {
        const day = insights && insights.day;
        const merged = day && day.merged ? day.merged.length : 0;
        const ahead = day && day.pushCommands ? day.pushCommands.length : 0;
        const title = day && !day.error ? `Day · ${merged} merged · ${state.review.length} in review${ahead ? ` · ${ahead} to push` : ""}` : "Day";
        return section(title, "dayOpen", saved, onToggle, (box) => {
            if (failed(box, day)) return;
            box.appendChild(el("div", "muted", `since ${clock(Date.parse(day.since))}`));
            day.merged.forEach((m) => {
                const line = el("div", "feed-item");
                line.append(el("span", "time", clock(Date.parse(m.at))), el("span", "kind", m.area), el("span", "text", `${m.branch} (${m.commit})`));
                box.appendChild(line);
            });
            if (!day.merged.length) box.appendChild(el("div", "muted", "nothing merged yet"));
            if (day.review.length) box.appendChild(el("div", "", `in review: ${day.review.map((e) => e.id).join(", ")}`));
            if (day.inProgress.length) box.appendChild(el("div", "", `in progress: ${day.inProgress.map((e) => e.id).join(", ")}`));
            const queued = Object.entries(day.queues || {}).filter(([, items]) => items.length);
            if (queued.length) box.appendChild(el("div", "muted", `queued: ${queued.map(([name, items]) => `${name} ${items.join(" ")}`).join("; ")}`));
            const t = insights.reviews && insights.reviews.total;
            if (t && t.reviews) {
                const share = Math.round((100 * t.firstTime) / Math.max(1, t.reviews));
                const most = (insights.reviews.most || []).slice(0, 3).map((m) => `${m.id} ${m.changes + m.remarks}`).join(", ");
                box.appendChild(el("div", "muted", `last ${insights.reviews.days} days: ${t.reviews} reviews of ${t.entries} entries, ${share}% approved at once, ${t.changes} sent back, ${t.remarks} remarks rounds${most ? `; most rounds: ${most}` : ""}`));
            }
            const push = el("div", "push");
            const lines = (day.push || []).filter((p) => p.ahead || p.behind || !p.upstream);
            lines.forEach((p) => {
                const text = !p.upstream ? `${p.repo} ${p.branch}: no upstream` : `${p.repo} ${p.branch}: ${p.ahead} ahead${p.behind ? `, ${p.behind} behind` : ""} of ${p.upstream}`;
                push.appendChild(el("div", p.behind ? "warn soft" : "", text));
            });
            if (!lines.length) push.appendChild(el("div", "muted", "everything is pushed (as of the last fetch)"));
            (day.pushCommands || []).forEach((c) => push.appendChild(el("code", "push-command", c)));
            box.appendChild(push);
            const actions = el("div", "actions");
            if (ahead) actions.appendChild(button(post, "Copy push commands", { cmd: "copyPush" }, "the git push commands, engine first (the game's pointer names its commit); the panel never pushes"));
            actions.appendChild(button(post, "End the day", { cmd: "endDay" }, "empty every worker queue (running loops finish their slice), copy the push commands; the Day counts from then", "danger"));
            actions.appendChild(button(post, "Dispatch", { cmd: "dispatch" }, "claude '/todo-dispatch' in the main checkout: it advises what each free worker takes, asks, and starts"));
            box.appendChild(actions);
        }, insights, "day");
    }

    const OUTCOME_TEXT = { review: "reached review", through: "slice approved, the next follows", limited: "stopped by the usage limit", running: "running", ended: "ended without review" };

    // Timeline: one lane per worker over the last hours, a bar per session coloured by how it ended.
    function timelineSection(insights, saved, onToggle, history, cores) {
        const tl = insights && insights.timeline;
        const title = tl && tl.lanes ? `Timeline · ${tl.hours} h · busy ${tl.lanes.map((l) => `${l.name} ${Math.round(100 * l.busyShare)}%`).join(", ")}` : "Timeline";
        return section(title, "timelineOpen", saved, onToggle, (box) => {
            if (failed(box, tl)) return;
            const width = 600;
            const labelW = 34;
            const laneH = 22;
            const top = 16;
            const span = Math.max(1, tl.now - tl.since);
            const x = (ms) => labelW + ((width - labelW - 4) * (Math.min(tl.now, Math.max(tl.since, ms)) - tl.since)) / span;
            // the load of the machine under the lanes (the panel's samples, one a minute), against its cores
            const samples = (history || []).filter((h) => h.at >= tl.since && h.at <= tl.now + 600000 && typeof h.load === "number");
            const loadH = samples.length >= 2 ? 44 : 0;
            const lanesBottom = top + tl.lanes.length * laneH;
            const height = lanesBottom + (loadH ? loadH + 8 : 0) + 4;
            const chart = svg("svg", { viewBox: `0 0 ${width} ${height}`, class: "timeline", role: "img", "aria-label": "sessions of each worker over time" });
            const step = tl.hours > 36 ? 6 : tl.hours > 12 ? 3 : 1;
            const first = new Date(tl.since);
            first.setMinutes(0, 0, 0);
            for (let t = first.getTime() + 3600000; t < tl.now; t += 3600000) {
                const hour = new Date(t).getHours();
                if (hour % step) continue;
                chart.appendChild(svg("line", { x1: x(t), x2: x(t), y1: top - 4, y2: height, class: hour === 0 ? "tl-grid tl-midnight" : "tl-grid" }));
                chart.appendChild(svg("text", { x: x(t), y: 10, class: "tl-hour", "text-anchor": "middle" }, String(hour).padStart(2, "0")));
            }
            tl.lanes.forEach((lane, i) => {
                const y = top + i * laneH;
                chart.appendChild(svg("text", { x: 0, y: y + 14, class: "tl-name" }, lane.name));
                lane.spans.forEach((s) => {
                    const x1 = x(s.start);
                    const x2 = Math.max(x1 + 2, x(s.outcome === "running" ? tl.now : s.end));
                    const bar = svg("rect", { x: x1, y: y + 3, width: x2 - x1, height: laneH - 8, rx: 3, class: `tl-bar tl-${s.outcome}` });
                    bar.appendChild(svg("title", {}, `${s.entry || "no entry"}\n${clock(s.start)}–${s.outcome === "running" ? "now" : clock(s.end)}: ${OUTCOME_TEXT[s.outcome] || s.outcome}${s.loop ? "" : " (not a /todo-loop session)"}`));
                    chart.appendChild(bar);
                    if (x2 - x1 > 60 && s.entry) {
                        const label = svg("text", { x: x1 + 4, y: y + 14, class: "tl-label" }, s.entry.length > (x2 - x1) / 6 ? `${s.entry.slice(0, Math.floor((x2 - x1) / 6) - 1)}…` : s.entry);
                        label.style.pointerEvents = "none";
                        chart.appendChild(label);
                    }
                });
            });
            if (loadH) {
                const y0 = lanesBottom + 6 + loadH;
                const maxLoad = Math.max(cores ? cores * 1.5 : 1, ...samples.map((h) => h.load));
                const y = (v) => y0 - (loadH * Math.min(v, maxLoad)) / maxLoad;
                const xs = (h) => x(Math.min(h.at, tl.now));
                chart.appendChild(svg("text", { x: 0, y: y0 - loadH / 2 + 3, class: "tl-name" }, "load"));
                const area = `M ${xs(samples[0])} ${y0} ` + samples.map((h) => `L ${xs(h)} ${y(h.load)}`).join(" ") + ` L ${xs(samples[samples.length - 1])} ${y0} Z`;
                const fill = svg("path", { d: area, class: "tl-load" });
                fill.appendChild(svg("title", {}, `load of the machine, 1 min average (most ${Math.round(Math.max(...samples.map((h) => h.load)))})${cores ? `; the dashed line is ${cores} cores` : ""}`));
                chart.appendChild(fill);
                const mem = samples.filter((h) => typeof h.mem === "number");
                if (mem.length >= 2) {
                    const line = svg("polyline", { points: mem.map((h) => `${xs(h)},${y0 - (loadH * h.mem) / 100}`).join(" "), class: "tl-mem" });
                    line.appendChild(svg("title", {}, "memory in use, % of the machine's"));
                    chart.appendChild(line);
                }
                if (cores && cores <= maxLoad) chart.appendChild(svg("line", { x1: labelW, x2: width - 4, y1: y(cores), y2: y(cores), class: "tl-cores" }));
            }
            box.appendChild(chart);
            const legend = el("div", "tl-legend muted");
            ["review", "through", "running", "limited", "ended"].forEach((k) => {
                const item = el("span", `tl-key tl-${k}`, OUTCOME_TEXT[k]);
                legend.appendChild(item);
            });
            if (loadH) {
                legend.appendChild(el("span", "tl-key tl-load-key", "load"));
                legend.appendChild(el("span", "tl-key tl-mem-key", "memory %"));
            }
            box.appendChild(legend);
            const overlaps = tl.lanes.filter((lane) => lane.spans.some((s, j) => lane.spans.slice(j + 1).some((o) => o.start < s.end - 60000)));
            if (overlaps.length) box.appendChild(el("div", "warn soft", `two sessions at once in ${overlaps.map((l) => l.name).join(", ")}: look at the bars that share a lane`));
        }, insights, "timeline");
    }

    // Busy files: what each taken entry holds, and why an open entry cannot go to a worker now.
    function busySection(insights, saved, onToggle, post) {
        const busy = insights && insights.busy;
        const free = busy && busy.open ? busy.open.filter((o) => !o.blocked && !o.meets.length && o.source).length : 0;
        const title = busy && busy.taken ? `Busy files · ${busy.taken.length} taken · ${free} open entries meet none` : "Busy files";
        return section(title, "busyOpen", saved, onToggle, (box) => {
            if (failed(box, busy)) return;
            busy.taken.forEach((t) => {
                const line = el("div", "busy-taken");
                line.appendChild(el("span", "entry", t.id));
                if (t.worker) line.appendChild(el("span", "muted", ` ${t.worker}`));
                line.appendChild(el("span", "muted", ` · ${t.status} · ${t.files.length} files`));
                const chips = el("div", "chips");
                t.folders.slice(0, 8).forEach((f) => chips.appendChild(el("span", "chip", `${f.path} ${f.count}`)));
                line.appendChild(chips);
                line.title = t.files.join("\n");
                box.appendChild(line);
            });
            const open = busy.open.filter((o) => !o.blocked);
            const groups = [
                ["meets a taken entry", open.filter((o) => o.meets.length)],
                ["free: its files meet nothing taken", open.filter((o) => !o.meets.length && o.source)],
                ["files unknown (no verdict, no paths in its text)", open.filter((o) => !o.meets.length && !o.source)],
            ];
            groups.forEach(([label, items]) => {
                if (!items.length) return;
                box.appendChild(el("div", "busy-group muted", `${label} (${items.length})`));
                items.slice(0, 25).forEach((o) => {
                    const line = el("div", "feed-item");
                    const id = el("span", "kind link", o.id);
                    id.title = o.title || "";
                    id.addEventListener("click", () => post({ cmd: "todoList", entry: o.id }));
                    const text = o.meets.length ? o.meets.map((m) => `${m.id}${m.worker ? ` (${m.worker})` : ""}: ${m.files.slice(0, 3).join(", ")}${m.count > 3 ? ` +${m.count - 3}` : ""}`).join("; ") : o.title || "";
                    line.append(id, el("span", "text muted", text));
                    box.appendChild(line);
                });
            });
            const actions = el("div", "actions");
            actions.appendChild(button(post, "Dispatch", { cmd: "dispatch" }, "claude '/todo-dispatch' in the main checkout: it classifies what it needs, advises and starts"));
            actions.appendChild(button(post, "Read again", { cmd: "insights" }, "worker.py busy, day, timeline and review-stats now"));
            box.appendChild(actions);
        }, insights, "busy");
    }

    function gb(bytes) {
        return `${(bytes / 2 ** 30).toFixed(1)} GB`;
    }

    const KIND_WORDS = { compile: "compiling", link: "linking", build: "build", test: "tests" };

    function kindsText(kinds) {
        return ["compile", "link", "test", "build"].filter((k) => kinds[k]).map((k) => `${KIND_WORDS[k]} ${kinds[k]}`).join(" · ");
    }

    // The load line under the toolbar (worker.py machine): amber when the load is well over the cores or memory is short.
    function machineLine(machine) {
        if (!machine || !machine.load) return null;
        const mem = machine.memory;
        const heavy = machine.load[0] > machine.cores * 1.5 || (mem && mem.pressure !== "normal");
        const parts = [`load ${machine.load[0].toFixed(1)} / ${machine.cores} cores`];
        if (mem) parts.push(`memory ${100 - mem.freePercent}% of ${Math.round(mem.total / 2 ** 30)} GB${mem.pressure !== "normal" ? ` (pressure ${mem.pressure})` : ""}`);
        if (mem && mem.swapUsed) parts.push(`swap ${gb(mem.swapUsed)}`);
        const line = el("div", heavy ? "warn soft machine" : "muted machine", `Machine: ${parts.join(" · ")}`);
        line.title = `1, 5 and 15 min load: ${machine.load.map((v) => v.toFixed(1)).join(", ")}. A load above the cores means processes wait for a core.`;
        return line;
    }

    // What a worker's folder runs now: compilers, linkers, tests, and their CPU and memory.
    function workerLoad(worker, machine) {
        const g = machine && (machine.groups || []).find((x) => x.group === `${machine.checkout}/${worker.name}`);
        if (!g || (!kindsText(g.kinds) && g.cpu < 5)) return null;
        const text = [kindsText(g.kinds), `${Math.round(g.cpu)}% CPU`, gb(g.rss)].filter(Boolean).join(" · ");
        return el("div", "muted runs", text);
    }

    // Machine: every checkout and worker on this machine (other games' too) by CPU, and the Claude sessions.
    function machineSection(machine, saved, onToggle) {
        const busy = (machine.groups || []).filter((g) => kindsText(g.kinds) || g.cpu >= 5);
        const title = `Machine · ${busy.length} folder${busy.length === 1 ? "" : "s"} at work`;
        return section(title, "machineOpen", saved, onToggle, (box) => {
            busy.forEach((g) => {
                const line = el("div", "feed-item");
                const mine = g.group.startsWith(`${machine.checkout}/`);
                line.append(el("span", mine ? "kind" : "kind muted", g.group), el("span", "text", [kindsText(g.kinds), `${Math.round(g.cpu)}% CPU`, gb(g.rss)].filter(Boolean).join(" · ")));
                box.appendChild(line);
            });
            if (!busy.length) box.appendChild(el("div", "muted", "no build or test runs in any checkout"));
            const c = machine.claude;
            if (c && c.count) box.appendChild(el("div", "muted", `Claude: ${c.count} processes · ${Math.round(c.cpu)}% CPU · ${gb(c.rss)}`));
            box.appendChild(el("div", "muted", "CPU is ps's average and misses the short compiler runs; the load counts them."));
        });
    }

    window.TodoInsights = { machineLine, workerLoad, machineSection, problemLines, diffLine, reviewLine, daySection, timelineSection, busySection };
})();
