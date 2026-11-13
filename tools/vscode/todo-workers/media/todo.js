// The webview of the Todo list tab: the entries of the todo store (todo.py list --all --json --body).
// Everything shown comes from the store, so it is put into the page as text only (textContent), never as
// HTML; the body is a small subset of markdown (paragraphs, bullets, **bold**, `code`) built from nodes.
(function () {
    const vscode = acquireVsCodeApi();
    const root = document.getElementById("root");
    const STATUSES = ["open", "in progress", "review", "deferred", "closed"];
    // The order of todo.py list: normal and unset are one rank; the field is missing before the service has it.
    const PRIORITY_RANK = { now: 0, high: 1, normal: 2, low: 3 };
    const priorityRank = (e) => (e.priority in PRIORITY_RANK ? PRIORITY_RANK[e.priority] : PRIORITY_RANK.normal);
    const saved = vscode.getState() || {};
    let payload = { loading: true };
    let filter = {
        area: saved.area || "checkout",
        statuses: new Set(saved.statuses || ["open", "in progress", "review", "deferred"]),
        text: saved.text || "",
    };
    const open = new Set(saved.open || []);

    function save() {
        vscode.setState({ area: filter.area, statuses: [...filter.statuses], text: filter.text, open: [...open] });
    }

    function el(tag, className, text) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined && text !== null) node.textContent = String(text);
        return node;
    }

    function button(label, message, title, kind) {
        const b = el("button", kind || "", label);
        if (title) b.title = title;
        b.addEventListener("click", (event) => {
            event.stopPropagation();
            vscode.postMessage(message);
        });
        return b;
    }

    function ago(value) {
        const ms = typeof value === "number" ? value : Date.parse(value || "");
        if (!Number.isFinite(ms)) return "";
        const s = Math.max(0, Math.round((Date.now() - ms) / 1000));
        if (s < 60) return `${s} s ago`;
        const m = Math.floor(s / 60);
        if (m < 60) return `${m} min ago`;
        const h = Math.floor(m / 60);
        return h < 48 ? `${h} h ago` : `${Math.floor(h / 24)} d ago`;
    }

    // `code` and **bold** inside one line of text.
    function inline(parent, text) {
        const parts = String(text).split(/(`[^`]+`|\*\*[^*]+\*\*)/);
        for (const part of parts) {
            if (!part) continue;
            if (part.startsWith("`") && part.endsWith("`") && part.length > 1) parent.appendChild(el("code", "", part.slice(1, -1)));
            else if (part.startsWith("**") && part.endsWith("**") && part.length > 3) parent.appendChild(el("strong", "", part.slice(2, -2)));
            else parent.appendChild(document.createTextNode(part));
        }
        return parent;
    }

    function markdown(text) {
        const box = el("div", "body");
        let list = null;
        let para = null;
        for (const raw of String(text || "").split("\n")) {
            const line = raw.trimEnd();
            const bullet = /^\s*[-*]\s+(.*)$/.exec(line);
            const heading = /^#{1,6}\s+(.*)$/.exec(line);
            if (!line.trim()) {
                list = null;
                para = null;
            } else if (bullet) {
                para = null;
                if (!list) list = box.appendChild(el("ul"));
                inline(list.appendChild(el("li")), bullet[1]);
            } else if (heading) {
                list = null;
                para = null;
                inline(box.appendChild(el("h3")), heading[1]);
            } else if (list && /^\s+/.test(line)) {
                inline(list.lastChild, " " + line.trim());
            } else {
                list = null;
                if (!para) para = box.appendChild(el("p"));
                else para.appendChild(document.createTextNode(" "));
                inline(para, line.trim());
            }
        }
        return box;
    }

    function matches(entry) {
        const areas = payload.areas;
        if (filter.area === "checkout" && areas && !areas.includes(entry.area)) return false;
        if (filter.area !== "checkout" && filter.area !== "all" && entry.area !== filter.area) return false;
        if (!filter.statuses.has(entry.status)) return false;
        const text = filter.text.trim().toLowerCase();
        if (!text) return true;
        return [entry.id, entry.title, entry.summary, entry.body].some((v) => v && String(v).toLowerCase().includes(text));
    }

    function controls(entries) {
        const bar = el("div", "toolbar todo-toolbar");
        const area = el("select");
        const areaNames = [...new Set(entries.map((e) => e.area))].sort();
        const options = [["checkout", payload.areas ? `this checkout (${payload.areas.join(", ")})` : "this checkout"], ["all", "all areas"], ...areaNames.map((a) => [a, a])];
        for (const [value, label] of options) {
            const option = el("option", "", label);
            option.value = value;
            if (value === filter.area) option.selected = true;
            area.appendChild(option);
        }
        area.addEventListener("change", () => {
            filter.area = area.value;
            save();
            render();
        });

        const search = el("input", "search");
        search.type = "search";
        search.placeholder = "id, title or text";
        search.value = filter.text;
        search.addEventListener("input", () => {
            filter.text = search.value;
            save();
            renderList();
        });

        const chips = el("span", "chips");
        for (const status of STATUSES) {
            const count = entries.filter((e) => e.status === status && matchesArea(e)).length;
            const chip = el("button", `chip status-${status.replace(" ", "-")}${filter.statuses.has(status) ? " on" : ""}`, `${status} ${count}`);
            chip.addEventListener("click", () => {
                if (filter.statuses.has(status)) filter.statuses.delete(status);
                else filter.statuses.add(status);
                save();
                render();
            });
            chips.appendChild(chip);
        }

        const info = el("span", "muted", payload.loading ? "loading ..." : payload.at ? `updated ${ago(payload.at)}` : "");
        bar.append(area, search, chips, info, button("New entry", { cmd: "newEntry" }, "add an entry to the store (todo-add format; similar titles are named first)", "primary"), button("Refresh", { cmd: "refresh" }));
        return bar;
    }

    // The queues of the workers: drag an entry to change the order of its worker's queue.
    function queues() {
        const workers = (payload.workers || []).filter((w) => (w.queue || []).length);
        if (!workers.length) return null;
        const box = el("div", "card queues");
        box.appendChild(el("div", "muted", "Queues (drag to reorder; the panel starts the first one when the worker is free)"));
        for (const w of workers) {
            const line = box.appendChild(el("div", "queue-line"));
            line.appendChild(el("span", "entry", w.name));
            const items = [...w.queue];
            items.forEach((id, index) => {
                const chip = line.appendChild(el("span", "badge queued drag", `${index + 1}. ${id}`));
                chip.draggable = true;
                chip.addEventListener("dragstart", (event) => event.dataTransfer.setData("text/plain", JSON.stringify({ k: w.k, index })));
                chip.addEventListener("dragover", (event) => event.preventDefault());
                chip.addEventListener("drop", (event) => {
                    event.preventDefault();
                    let from;
                    try {
                        from = JSON.parse(event.dataTransfer.getData("text/plain"));
                    } catch (error) {
                        return;
                    }
                    if (from.k !== w.k || from.index === index) return;
                    const order = [...items];
                    const [moved] = order.splice(from.index, 1);
                    order.splice(index, 0, moved);
                    vscode.postMessage({ cmd: "reorderQueue", worker: w.k, ids: order });
                });
            });
        }
        return box;
    }

    function matchesArea(entry) {
        if (filter.area === "checkout") return !payload.areas || payload.areas.includes(entry.area);
        return filter.area === "all" || entry.area === filter.area;
    }

    function row(entry) {
        const item = el("div", `card todo-entry${open.has(entry.id) ? " open" : ""}`);
        const top = el("div", "card-top");
        top.append(
            el("span", "entry", entry.id),
            el("span", "area", entry.area),
            el("span", `badge status status-${String(entry.status).replace(" ", "-")}`, entry.status)
        );
        if (entry.priority in PRIORITY_RANK) top.appendChild(el("span", `badge prio prio-${entry.priority}`, entry.priority));
        if (entry.stage !== null && entry.stage !== undefined) top.appendChild(el("span", "muted", `stage ${entry.stage}`));
        if (entry.slices && entry.slices.count > 1) top.appendChild(el("span", "muted", `${entry.slices.count} slices ${entry.slices.done}/${entry.slices.total}`));
        else if (entry.planProgress && entry.planProgress.total) top.appendChild(el("span", "muted", `plan ${entry.planProgress.done}/${entry.planProgress.total}`));
        if (entry.slicing === "through") top.appendChild(el("span", "badge through", entry.costCap ? `through · cap $${entry.costCap}` : "through"));
        const blockers = (entry.blockedBy || []).filter((b) => b.status !== "closed");
        if (blockers.length) top.appendChild(el("span", "badge blocked", `blocked by ${blockers.map((b) => b.id).join(", ")}`));
        top.appendChild(el("span", "time", ago(entry.updatedAt)));
        item.appendChild(top);
        item.appendChild(inline(el("div", "title"), entry.title || ""));
        if (entry.statusNote || entry.branch) item.appendChild(el("div", "note", [entry.branch, entry.statusNote].filter(Boolean).join(": ")));
        const holder = (payload.workers || []).find((w) => w.active && w.entry === entry.id);
        if (holder) top.insertBefore(el("span", "badge busy", holder.name), top.lastChild);
        const away = (payload.elsewhere || []).includes(entry.id);
        const taken = [entry.origin ? `taken in ${entry.origin}` : "", entry.base ? `base ${entry.base}` : ""].filter(Boolean).join(", ");
        if (away) {
            const badge = el("span", "badge away", "branch elsewhere");
            if (taken) badge.title = taken;
            top.insertBefore(badge, top.lastChild);
        }
        if (away) item.appendChild(el("div", "note", "its branch lives in another checkout or on another machine (or was cut from another base): review, merge and continue it there"));
        else if (!holder && (entry.status === "open" || entry.status === "in progress")) {
            const actions = item.appendChild(el("div", "actions"));
            actions.appendChild(startButton(entry));
            const queued = (payload.workers || []).find((w) => (w.queue || []).includes(entry.id));
            if (queued) {
                top.insertBefore(el("span", "badge queued", `queued in ${queued.name}`), top.lastChild);
                actions.appendChild(button("Unqueue", { cmd: "unqueue", entry: entry.id, worker: queued.k }, `take it out of the queue of ${queued.name}`));
            } else if (entry.status === "open") {
                actions.appendChild(button("Queue for worker", { cmd: "queue", entry: entry.id }, "the panel starts it in the worker you pick as soon as that worker is free (its loop ended, or its entry reached review); a check for busy entries that name the same files comes first"));
            }
        }

        if (open.has(entry.id)) {
            if (taken && (entry.status === "in progress" || entry.status === "review")) item.appendChild(el("div", "muted", taken));
            item.appendChild(markdown(entry.body));
            const actions = el("div", "actions");
            actions.appendChild(button("Markdown", { cmd: "show", entry: entry.id }, "todo.py show in a markdown preview"));
            const plan = entry.planShown || entry.plan;
            if (plan) actions.appendChild(button("Plan", { cmd: "plan", name: plan }, `the plan ${plan} (the local copy in ~/.claude/plans, else from the store)`));
            actions.appendChild(button("Copy id", { cmd: "copy", text: entry.id }));
            item.appendChild(actions);
            item.appendChild(editActions(entry));
            item.appendChild(dependencies(entry));
        } else if (entry.summary) {
            item.appendChild(inline(el("div", "summary muted"), entry.summary));
        }
        item.addEventListener("click", (event) => {
            if (window.getSelection && String(window.getSelection())) return; // selecting text is no click
            if (event.target.closest("button")) return;
            if (open.has(entry.id)) open.delete(entry.id);
            else open.add(entry.id);
            save();
            renderList();
        });
        return item;
    }

    // The owner's writes: every one goes through todo.py (the panel asks first where it matters).
    function editActions(entry) {
        const actions = el("div", "actions");
        if (entry.status === "open") actions.appendChild(button("Defer", { cmd: "setStatus", entry: entry.id, status: "deferred" }, "status deferred: the loop does not take it"));
        if (entry.status === "deferred") actions.appendChild(button("Open", { cmd: "setStatus", entry: entry.id, status: "open" }, "status open: the loop may take it"));
        actions.appendChild(button("Stage", { cmd: "setStage", entry: entry.id, area: entry.area }, "the roadmap stage of its area, or none"));
        actions.appendChild(button("Priority", { cmd: "setPriority", entry: entry.id }, `the priority: now, high, normal, low or none (now ${entry.priority || "none"}); the loop and dispatch take the higher first`));
        actions.appendChild(button("Edit text", { cmd: "editBody", entry: entry.id }, "edit the body in the editor, then store it (refused when the entry changed meanwhile)"));
        const through = entry.slicing === "through";
        actions.appendChild(button(through ? "Through: on" : "Through: off", { cmd: "setThrough", entry: entry.id, on: !through }, through
            ? "the loop starts the next slice on the same branch without waiting for your merge; click to turn it off"
            : "turn on: slices of this entry go on one branch, each reviewed, and you merge once (an optional cost cap is asked)", through ? "primary" : ""));
        return actions;
    }

    function dependencies(entry) {
        const box = el("div", "deps");
        const blockedBy = entry.blockedBy || [];
        box.appendChild(el("span", "muted", blockedBy.length ? "waits for:" : "waits for nothing"));
        for (const link of blockedBy) {
            const chip = box.appendChild(el("span", `badge dep status-${String(link.status).replace(" ", "-")}`, `${link.id} (${link.status})`));
            if (link.title) chip.title = link.title + (link.note ? ` · ${link.note}` : "");
            chip.appendChild(button("×", { cmd: "depRm", entry: entry.id, blocker: link.id }, `${entry.id} no longer waits for ${link.id}`, "link"));
        }
        box.appendChild(button("+ waits for", { cmd: "depAdd", entry: entry.id }, "pick an entry this one waits for (the store refuses a cycle)", "link"));
        const blocks = entry.blocks || [];
        if (blocks.length) box.appendChild(el("span", "muted", `blocks: ${blocks.map((b) => `${b.id} (${b.status})`).join(", ")}`));
        return box;
    }

    // Open entries, and ones in progress that no running worker holds (the loop continues their branch).
    function startButton(entry) {
        const free = (payload.workers || []).filter((w) => !w.active && !w.dirty && !w.busy);
        const title = payload.workers
            ? free.length
                ? `/todo-loop 1 ${entry.id} as the background session of a free worker (${free.map((w) => w.name).join(", ")}): you pick which`
                : "no free worker now: every one runs a session or has uncommitted changes"
            : "the workers are not known yet";
        const b = button("Attach to free worker", { cmd: "startInWorker", entry: entry.id }, title, free.length ? "primary" : "");
        if (!free.length) b.disabled = true;
        return b;
    }

    let list = null;

    function renderList() {
        if (!list) return;
        list.replaceChildren();
        const entries = (payload.entries || []).filter(matches);
        const order = (e) => STATUSES.indexOf(e.status);
        entries.sort((a, b) => order(a) - order(b) || priorityRank(a) - priorityRank(b) || String(b.updatedAt).localeCompare(String(a.updatedAt)));
        if (!entries.length) list.appendChild(el("p", "muted", payload.entries ? "No entry matches." : ""));
        for (const entry of entries) list.appendChild(row(entry));
    }

    function render() {
        const focused = document.activeElement && document.activeElement.classList.contains("search");
        root.replaceChildren();
        const entries = payload.entries || [];
        root.appendChild(controls(entries));
        if (payload.error) root.appendChild(el("div", "error", payload.error));
        const queueBox = queues();
        if (queueBox) root.appendChild(queueBox);
        list = root.appendChild(el("div", "todo-list"));
        if (!payload.entries && payload.loading) list.appendChild(el("p", "muted", "Asking todo.py ..."));
        else renderList();
        if (focused) {
            const search = root.querySelector(".search");
            search.focus();
            search.setSelectionRange(search.value.length, search.value.length);
        }
    }

    window.addEventListener("message", (event) => {
        payload = { ...payload, ...event.data };
        render();
    });
    render();
    vscode.postMessage({ cmd: "ready" });
})();
