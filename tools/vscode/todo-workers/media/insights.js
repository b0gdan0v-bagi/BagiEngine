// The parts of the Workers panel built from worker.py's slower views (tools/todo/worker_insights.py) and from the
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
    function timelineSection(insights, saved, onToggle) {
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
            const height = top + tl.lanes.length * laneH + 4;
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
            box.appendChild(chart);
            const legend = el("div", "tl-legend muted");
            ["review", "through", "running", "limited", "ended"].forEach((k) => {
                const item = el("span", `tl-key tl-${k}`, OUTCOME_TEXT[k]);
                legend.appendChild(item);
            });
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

    window.TodoInsights = { problemLines, diffLine, reviewLine, daySection, timelineSection, busySection };
})();
