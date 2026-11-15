// What the panel says about a run, as pure functions of the rows of `worker.py list --json` (and of the entries of
// `todo.py list --json`): the words of a run that took nothing, the progress of an entry, and the guards of the queue
// and of a merge. Shared by the extension (src/) and the webviews, and driven by node in test/ without VS Code.
(function (root, factory) {
    if (typeof module === "object" && module.exports) module.exports = factory();
    else root.TodoRuns = factory();
})(typeof self !== "undefined" ? self : this, function () {
    const NO_REASON = "no reason said: Log shows the run";

    // The progress an entry shows, as the bar's fill and its words, from the `slices` and `planProgress` of the store.
    // A through entry is one branch over many slices: `slices.by` is "stages" when it has a stage plan (the whole is
    // the stages and the slice plan is "this slice"), else its slice plans of the past say nothing about it. An entry
    // cut slice by slice shows its slice plans together. A shown plan that is the stage plan has no slice to show.
    function progressOf(entry) {
        if (!entry) return null;
        const slices = entry.slices || null;
        const p = entry.planProgress && entry.planProgress.total ? entry.planProgress : null;
        const stages = slices && slices.by === "stages" && slices.total ? slices : null;
        const all = slices && slices.by !== "stages" && slices.count > 1 && slices.total ? slices : null;
        const sliceShown = Boolean(entry.planShown) && entry.planShown === entry.stagesPlan ? null : p;
        const slice = sliceShown ? `this slice ${sliceShown.done}/${sliceShown.total}` : "";
        if (entry.slicing === "through") {
            if (stages) return { done: stages.done, total: stages.total, text: [`stage ${stages.done}/${stages.total}`, slice].filter(Boolean).join(" · ") };
            return sliceShown ? { done: sliceShown.done, total: sliceShown.total, text: slice } : null;
        }
        if (all) return { done: all.done, total: all.total, text: `${all.count} slices ${all.done}/${all.total}${sliceShown ? ` · ${slice}` : ""}` };
        return p ? { done: p.done, total: p.total, text: `plan ${p.done}/${p.total}` } : null;
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

    return { NO_REASON, progressOf, progressText, unlinkedWaits, queueGuard, idsToAddBack, loopsOnBase };
});
