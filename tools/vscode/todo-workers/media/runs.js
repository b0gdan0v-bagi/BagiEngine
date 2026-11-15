// What the panel says about a run, as pure functions of the rows of `worker.py list --json` (and of the entries of
// `todo.py list --json`): a run that took nothing, and the progress of an entry. Shared by the extension (src/) and the
// webviews, and driven by node in test/ without VS Code.
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

    return { NO_ENTRY, REASON_MAX, parseNotTaken, noClaimOf, nextMemory, sessionIdOf };
});
