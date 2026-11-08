// The webview of the Todo list tab: the entries of the todo store (todo.py list --all --json --body).
// Everything shown comes from the store, so it is put into the page as text only (textContent), never as
// HTML; the body is a small subset of markdown (paragraphs, bullets, **bold**, `code`) built from nodes.
(function () {
    const vscode = acquireVsCodeApi();
    const root = document.getElementById("root");
    const STATUSES = ["open", "in progress", "review", "deferred", "closed"];
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
        bar.append(area, search, chips, info, button("Refresh", { cmd: "refresh" }));
        return bar;
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
        if (entry.stage !== null && entry.stage !== undefined) top.appendChild(el("span", "muted", `stage ${entry.stage}`));
        if (entry.planProgress && entry.planProgress.total) top.appendChild(el("span", "muted", `plan ${entry.planProgress.done}/${entry.planProgress.total}`));
        top.appendChild(el("span", "time", ago(entry.updatedAt)));
        item.appendChild(top);
        item.appendChild(inline(el("div", "title"), entry.title || ""));
        if (entry.statusNote || entry.branch) item.appendChild(el("div", "note", [entry.branch, entry.statusNote].filter(Boolean).join(": ")));
        const holder = (payload.workers || []).find((w) => w.active && w.entry === entry.id);
        if (holder) top.insertBefore(el("span", "badge busy", holder.name), top.lastChild);
        if (!holder && (entry.status === "open" || entry.status === "in progress")) item.appendChild(el("div", "actions")).appendChild(startButton(entry));

        if (open.has(entry.id)) {
            item.appendChild(markdown(entry.body));
            const actions = el("div", "actions");
            actions.appendChild(button("Markdown", { cmd: "show", entry: entry.id }, "todo.py show in a markdown preview"));
            const plan = entry.planShown || entry.plan;
            if (plan) actions.appendChild(button("Plan", { cmd: "plan", name: plan }, `the plan ${plan} (the local copy in ~/.claude/plans, else from the store)`));
            actions.appendChild(button("Copy id", { cmd: "copy", text: entry.id }));
            item.appendChild(actions);
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
        entries.sort((a, b) => order(a) - order(b) || String(b.updatedAt).localeCompare(String(a.updatedAt)));
        if (!entries.length) list.appendChild(el("p", "muted", payload.entries ? "No entry matches." : ""));
        for (const entry of entries) list.appendChild(row(entry));
    }

    function render() {
        const focused = document.activeElement && document.activeElement.classList.contains("search");
        root.replaceChildren();
        const entries = payload.entries || [];
        root.appendChild(controls(entries));
        if (payload.error) root.appendChild(el("div", "error", payload.error));
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
