// What the panel says about a run, as pure functions of the rows of `worker.py list --json` (and of the entries of
// `todo.py list --json`): a run that took nothing, the progress of an entry, and the guards of the queue and of a
// merge. Shared by the extension (src/) and the webviews, and driven by node in test/ without VS Code.
(function (root, factory) {
    if (typeof module === "object" && module.exports) module.exports = factory();
    else root.TodoRuns = factory();
})(typeof self !== "undefined" ? self : this, function () {
    // The last line of a /todo-loop run that claimed no entry (.claude/skills/todo-loop/SKILL.md, "Report"). worker.py
    // folds a `say` event into one line, so the line may follow other text and is the tail of the event.
    const NOT_TAKEN = /(?:^|\s)NOT TAKEN (\S+): (.+)$/gm;
    const NO_ENTRY = "-";
    const REASON_MAX = 160;
    // A run without the line still says why in a word of this list; only the newest of the last lines counts.
    const WHY_WORDS = /BLOCKED|blocked by|waits for|taken|claimed|nothing/i;
    const WHY_LINES = 3;
    const NO_REASON = "no reason said: Log shows the run";

    function says(events) {
        return (Array.isArray(events) ? events : []).filter((e) => e && e.kind === "say" && typeof e.text === "string");
    }

    function clip(text) {
        const s = String(text).replace(/\[([^\]]+)\]\([^)\s]+\)/g, "$1").trim();
        return s.length <= REASON_MAX ? s : `${s.slice(0, REASON_MAX - 1)}…`;
    }

    // {entry: id | null, reason} of the newest NOT TAKEN line in the `say` events, else null.
    function parseNotTaken(events) {
        const list = says(events);
        for (let i = list.length - 1; i >= 0; i--) {
            const found = [...list[i].text.matchAll(NOT_TAKEN)];
            if (!found.length) continue;
            const last = found[found.length - 1];
            return { entry: last[1] === NO_ENTRY ? null : last[1], reason: clip(last[2]) };
        }
        return null;
    }

    function fallbackReason(events) {
        const newest = says(events).slice(-WHY_LINES).reverse();
        const hit = newest.find((e) => WHY_WORDS.test(e.text));
        return hit ? clip(hit.text) : null;
    }

    // The identity of a worker's session, stable for as long as it lives.
    function sessionIdOf(worker) {
        const s = worker && worker.session;
        return (s && (s.id || s.name)) || null;
    }

    // What to remember of a worker between two reads (extension globalState): the entry a running loop was started on
    // and has not claimed, because worker.py drops that entry when the session ends. `previous` is what was
    // remembered of this worker. Dropped when another session shows or the worker holds an entry.
    function nextMemory(previous, worker) {
        const sid = sessionIdOf(worker);
        if (!sid) return null;
        const entry = worker.entry;
        if (worker.active && entry && entry.starting) return { sessionId: sid, entry: { id: entry.id, title: entry.title || "" } };
        if (previous && previous.sessionId === sid && !entry) return previous;
        return null;
    }

    // {entry: id | null, title, reason: string | null, source: "line" | "memory"} for a finished run that held and
    // took nothing, else null. `remembered` is the nextMemory of this worker.
    function noClaimOf(worker, remembered) {
        const s = worker && worker.session;
        if (!s || !((s.state === "done") || worker.endedTurn)) return null;
        if (worker.stopped || worker.limited || worker.entry) return null;
        const events = worker.activity && worker.activity.events;
        const mine = remembered && remembered.sessionId === sessionIdOf(worker) ? remembered.entry : null;
        const line = parseNotTaken(events);
        if (line) return { entry: line.entry, title: mine && mine.id === line.entry ? mine.title : "", reason: line.reason, source: "line" };
        if (mine) return { entry: mine.id, title: mine.title || "", reason: fallbackReason(events), source: "memory" };
        return null;
    }

    // The progress an entry shows, as the bar's fill and its words. A through entry is one branch over many slices:
    // its slice plans of the past say nothing about it, so the whole is the stage plan (when it has one) and the
    // slice plan is "this slice". An entry cut slice by slice shows its slice plans together.
    function progressOf(entry) {
        if (!entry) return null;
        const p = entry.planProgress && entry.planProgress.total ? entry.planProgress : null;
        const stages = entry.stages && entry.stages.total ? entry.stages : null;
        const all = entry.slices && entry.slices.count > 1 && entry.slices.total ? entry.slices : null;
        const slice = p ? `this slice ${p.done}/${p.total}` : "";
        if (entry.slicing === "through") {
            if (stages) return { done: stages.done, total: stages.total, text: [`stage ${stages.done}/${stages.total}`, slice].filter(Boolean).join(" · ") };
            return p ? { done: p.done, total: p.total, text: slice } : null;
        }
        if (all) return { done: all.done, total: all.total, text: `${all.count} slices ${all.done}/${all.total}${p ? ` · ${slice}` : ""}` };
        return p ? { done: p.done, total: p.total, text: `plan ${p.done}/${p.total}` } : null;
    }

    // The newest stage plan of each entry in the answer of `todo.py plan list --kind stages --json`:
    // Map of entry id -> {name, done, total}.
    function newestStages(plans) {
        const newest = new Map();
        for (const plan of Array.isArray(plans) ? plans : []) {
            const p = plan && plan.progress;
            if (!plan || !plan.entry || !p || !p.total) continue;
            const held = newest.get(plan.entry);
            if (!held || String(plan.updatedAt || "") > String(held.updatedAt || "")) newest.set(plan.entry, plan);
        }
        return new Map([...newest].map(([id, plan]) => [id, { name: plan.name, done: plan.progress.done, total: plan.progress.total }]));
    }

    function progressText(entry) {
        const shown = progressOf(entry);
        return shown ? shown.text : "";
    }

    // The line of an entry body that names what it waits for, bold or not (`**Waits for:** a, b and c`).
    const WAITS_FOR = /^[ \t>]*(?:[-*][ \t]+)?\*{0,2}Waits for(?::\*{0,2}|\*{0,2}:)(.*)$/gim;
    const ID_WORD = /[A-Za-z0-9][A-Za-z0-9_-]*/g;

    function lookup(byId, id) {
        if (!byId) return undefined;
        return typeof byId.get === "function" ? byId.get(id) : byId[id];
    }

    // The ids a body names after `**Waits for:**` that exist in the store, are not closed and are not a dependency
    // link of the entry yet: what the owner has to link (`todo.py dep add`) for the queue and the loop to see it.
    // `byId` is a Map or an object of id -> entry. A word counts only when it is exactly an id of the store.
    function unlinkedWaits(entry, byId) {
        if (!entry || typeof entry.body !== "string") return [];
        const linked = new Set((entry.blockedBy || []).map((b) => (b && typeof b === "object" ? b.id : b)));
        const found = [];
        for (const line of entry.body.matchAll(WAITS_FOR)) {
            for (const word of line[1].replace(/`/g, " ").match(ID_WORD) || []) {
                const other = lookup(byId, word);
                if (!other || word === entry.id || linked.has(word) || other.status === "closed" || found.includes(word)) continue;
                found.push(word);
            }
        }
        return found;
    }

    // Why an entry should not be queued as it is. `show` is the entry as `todo.py show --json` gives it (with `origin`
    // and `base` of its claim), `elsewhere` the ids of `worker.py list` whose branch lives in another checkout.
    // {kind: "elsewhere", origin, base}: refuse. {kind: "blocked", blockers: [{id, status}]}: ask, because a stacked
    // or paired entry is takeable and the start rule of the panel keeps the others queued. null: queue it.
    function queueGuard(show, elsewhere) {
        if (!show) return null;
        if ((show.status === "in progress" || show.status === "review") && (elsewhere || []).includes(show.id)) {
            return { kind: "elsewhere", origin: show.takenOrigin || show.origin || null, base: show.takenBase || show.base || null };
        }
        const blockers = (show.blockedBy || []).filter((b) => b && b.status !== "closed").map((b) => ({ id: b.id, status: b.status }));
        return blockers.length ? { kind: "blocked", blockers } : null;
    }

    // The ids of `wanted` that a queue no longer holds, in the wanted order: what a failed clear or add lost.
    function idsToAddBack(wanted, current) {
        const held = new Set(current || []);
        return (wanted || []).filter((id) => !held.has(id));
    }

    // The workers that run a loop on the base the merge of `repoRel` moves: a session running, the head of that repository
    // detached or on `base`, no `todo/*` branch. `repoRel` is the repository relative to the checkout ("." or "engine").
    function loopsOnBase(workers, repoRel, base) {
        const found = [];
        for (const w of workers || []) {
            const head = w && w.active ? (repoRel === "." ? w.head : repoRel === "engine" ? w.engine : null) : null;
            if (!head) continue;
            if (head.branch && head.branch !== base) continue;
            found.push({ name: w.name, commit: head.commit || "" });
        }
        return found;
    }

    return { NO_ENTRY, NO_REASON, REASON_MAX, parseNotTaken, noClaimOf, nextMemory, sessionIdOf, progressOf, progressText, newestStages, unlinkedWaits, queueGuard, idsToAddBack, loopsOnBase };
});
