// The flow graph of the workers, shared by the Workers panel and the Todo list tab: one lane per worker (the worker,
// the entry it holds, then its queue in order) and dashed arrows from a blocker to the entry it blocks, drawn as
// inline SVG. Input is the `workers` array of `worker.py list --json --store` (or the same fields as the Todo list
// gets them); `wait` and each blocker's `kind` come from worker.py (`queue_relation`), never decided here.
// Everything shown is text from the store or the transcripts, so it goes in as text nodes only.
// The layout is built as a plain tree first, so node can turn it into an SVG string (a sample render) without a DOM.
(function (root, factory) {
    if (typeof module === "object" && module.exports) module.exports = factory();
    else root.TodoGraph = factory();
})(typeof self !== "undefined" ? self : this, function () {
    const SVG_NS = "http://www.w3.org/2000/svg";
    const WORKER_W = 90;
    const NODE_W = 160;
    const NODE_H = 44;
    const GAP_X = 30; // the flow arrow between two boxes of a lane
    const GAP_Y = 34; // between lanes: room for the dependency arcs and their labels
    const PAD = 8;
    const TOP = 30; // above the first lane: the arcs between boxes of that lane
    const ID_CHAR_PX = 6.8; // rough widths of one character (the monospace id, the smaller second line), for truncation
    const SUB_CHAR_PX = 5.7;
    const STREAK_LIMIT = 4; // worker.py QUEUE_STREAK_LIMIT: the queue pauses after this many entries in a row
    const DRAG_PX = 5;
    const KIND_LABEL = { stacked: "stacked on", paired: "paired", waits: "waits for" };

    let dragging = false;

    function slug(text) {
        return String(text || "none").replace(/[^A-Za-z0-9]+/g, "-").toLowerCase();
    }

    function trunc(text, px, charPx) {
        const s = String(text || "");
        const max = Math.max(3, Math.floor(px / (charPx || SUB_CHAR_PX)));
        return s.length <= max ? s : `${s.slice(0, max - 1)}…`;
    }

    function minutes(startedAt) {
        if (typeof startedAt !== "number" || !startedAt) return "";
        const m = Math.max(0, Math.floor((Date.now() - startedAt) / 60000));
        return m >= 60 ? `${Math.floor(m / 60)}h${String(m % 60).padStart(2, "0")}` : `${m} min`;
    }

    // The state of the worker's session as the cards name it (panel.js sessionBadge), in a word for a small box.
    function sessionOf(w) {
        const s = w.session;
        if (w.limited) return { kind: "limited", word: w.limited.resets ? `limit · ${w.limited.resets}` : "usage limit" };
        if (w.stopped) return { kind: "stopped", word: "stopped" };
        if ((s && s.state === "done") || w.endedTurn) return { kind: "done", word: "done" };
        if (w.waiting) return { kind: "waiting", word: "waits for you" };
        if (s && s.status === "busy") return { kind: "busy", word: s.startedAt ? `busy · ${minutes(s.startedAt)}` : "busy" };
        if (s) return { kind: "idle", word: "idle" };
        return { kind: "none", word: "no session" };
    }

    function progressOf(entry) {
        const all = entry.slices && entry.slices.count > 1 ? entry.slices : null;
        const p = entry.planProgress;
        if (all && all.total) return `${all.done}/${all.total}`;
        return p && p.total ? `plan ${p.done}/${p.total}` : "";
    }

    function openBlockers(entry) {
        return (entry && entry.blockedBy ? entry.blockedBy : []).filter((b) => b && b.status !== "closed");
    }

    function kindLabel(blocker) {
        if (blocker.kind === "waits" && blocker.status === "review") return "waits for merge";
        return KIND_LABEL[blocker.kind] || "waits for";
    }

    // --- the tree: {tag, attrs, text, kids, act} ------------------------------------------------------------------
    function n(tag, attrs, kids, text) {
        return { tag, attrs: attrs || {}, kids: kids || [], text };
    }

    function tip(text) {
        return n("title", {}, [], text);
    }

    function textNode(x, y, cls, text) {
        return n("text", { x, y, class: cls }, [], text);
    }

    function colX(col) {
        return col === 0 ? PAD : PAD + WORKER_W + GAP_X + (col - 1) * (NODE_W + GAP_X);
    }

    function laneY(lane) {
        return TOP + lane * (NODE_H + GAP_Y);
    }

    function box(x, y, w, cls, stripe) {
        const kids = [n("rect", { x, y, width: w, height: NODE_H, rx: 5, class: "g-frame" })];
        if (stripe) kids.push(n("rect", { x: x + 0.5, y: y + 0.5, width: 4, height: NODE_H - 1, rx: 2, class: "g-stripe" }));
        return n("g", { class: cls }, kids);
    }

    function flowArrow(x1, x2, y, cls) {
        return n("line", { x1, y1: y, x2: x2 - 2, y2: y, class: `g-flow ${cls || ""}`.trim(), "marker-end": "url(#g-arrow-flow)" });
    }

    // A dependency arrow from the blocker's box to the blocked one's: an arc above the lane inside one lane, an S
    // curve between lanes. Returns the path and its label at the curve's middle.
    function depArrow(from, to, blocker) {
        const kind = blocker.kind || "waits";
        let d;
        let mid;
        if (from.lane === to.lane) {
            const x1 = from.x + from.w / 2;
            const x2 = to.x + to.w / 2;
            const y = from.y;
            const h = 26;
            d = `M ${x1} ${y} C ${x1} ${y - h}, ${x2} ${y - h}, ${x2} ${y - 3}`;
            mid = { x: (x1 + x2) / 2, y: y - 0.75 * h - 3 };
        } else {
            const down = from.lane < to.lane;
            const x1 = from.x + from.w / 2 + (down ? 10 : -10);
            const x2 = to.x + to.w / 2 + (down ? -10 : 10);
            const y1 = down ? from.y + NODE_H : from.y;
            const y2 = down ? to.y - 3 : to.y + NODE_H + 3;
            const bend = Math.max(24, Math.abs(y2 - y1) / 2);
            const c1 = down ? y1 + bend : y1 - bend;
            const c2 = down ? y2 - bend : y2 + bend;
            d = `M ${x1} ${y1} C ${x1} ${c1}, ${x2} ${c2}, ${x2} ${y2}`;
            mid = { x: (x1 + 3 * x1 + 3 * x2 + x2) / 8, y: (y1 + 3 * c1 + 3 * c2 + y2) / 8 + 3 };
        }
        const label = kindLabel(blocker);
        return n("g", { class: `g-dep kind-${slug(kind)}` }, [
            n("path", { d, class: "g-dep-line", "marker-end": `url(#g-arrow-${slug(kind)})` }),
            textNode(mid.x, mid.y, "g-dep-label", label),
            tip(`${to.id} ${label} ${blocker.id}${blocker.status ? ` (${blocker.status})` : ""}`),
        ]);
    }

    function pauseMark(x, y, reason, streak) {
        return n("g", { class: "g-pause" }, [
            n("circle", { cx: x, cy: y, r: 8 }),
            n("rect", { x: x - 3.5, y: y - 4, width: 2.5, height: 8 }),
            n("rect", { x: x + 1, y: y - 4, width: 2.5, height: 8 }),
            tip(`The queue is paused: ${reason}. Nothing is started from it until you queue, resume or clear it (or a merge of the entry this worker held).${streak.length ? `\nStarted in a row: ${streak.join(", ")}` : ""}`),
        ]);
    }

    function defs() {
        const marker = (id) =>
            n("marker", { id: `g-arrow-${id}`, viewBox: "0 0 8 8", refX: 7, refY: 4, markerWidth: 7, markerHeight: 7, orient: "auto-start-reverse" }, [
                n("path", { d: "M 0 0 L 8 4 L 0 8 z", class: `g-head g-head-${id}` }),
            ]);
        return n("defs", {}, ["flow", "stacked", "paired", "waits"].map(marker));
    }

    // workers: the rows of list --json (or the Todo list's copy of them). Returns {tree, width, height}.
    function build(workers) {
        const lanes = (workers || []).slice().sort((a, b) => a.k - b.k);
        const pos = new Map(); // entry id -> its box
        const boxes = [];
        const flows = [];
        const extra = [];
        const blocked = []; // {box, blockers}
        let cols = 2;
        lanes.forEach((w, lane) => {
            const y = laneY(lane);
            const mid = y + NODE_H / 2;
            const sess = sessionOf(w);
            const wbox = box(colX(0), y, WORKER_W, `g-node g-worker sess-${sess.kind}`, false);
            wbox.kids.push(textNode(colX(0) + 9, y + 18, "g-name", w.name), textNode(colX(0) + 9, y + 34, "g-sub", trunc(sess.word, WORKER_W - 14)));
            wbox.kids.push(tip(`${w.name}: ${sess.word}${w.entry ? `, holds ${w.entry.id}` : ", holds no entry"}`));
            wbox.act = { type: "worker", worker: w.k, name: w.name, entry: w.entry ? w.entry.id : null };
            boxes.push(wbox);

            const cx = colX(1);
            flows.push(flowArrow(colX(0) + WORKER_W, cx, mid));
            const entry = w.entry;
            if (entry) {
                // a loop started on this entry that has not claimed it yet (worker.py list: starting)
                const status = entry.starting ? "starting" : entry.status || "?";
                const over = ["stopped", "done", "waiting", "limited"].includes(sess.kind) ? sess : null;
                const ebox = box(cx, y, NODE_W, `g-node g-entry g-held st-${slug(status)}${over ? ` over-${over.kind}` : ""}`, true);
                const sub = [status, over ? over.word : "", progressOf(entry)].filter(Boolean).join(" · ");
                ebox.kids.push(textNode(cx + 11, y + 18, "g-id", trunc(entry.id, NODE_W - 18, ID_CHAR_PX)), textNode(cx + 11, y + 34, "g-sub", trunc(sub, NODE_W - 18)));
                ebox.kids.push(tip([`${entry.id} (${status}, held by ${w.name}, session ${sess.word})`, entry.title, entry.statusNote].filter(Boolean).join("\n")));
                ebox.act = { type: "entry", entry: entry.id, worker: w.k, name: w.name, held: true };
                boxes.push(ebox);
                const at = { id: entry.id, x: cx, y, w: NODE_W, lane };
                pos.set(entry.id, at);
                blocked.push({ at, blockers: openBlockers(entry) });
            } else {
                const ghost = box(cx, y, NODE_W, "g-node g-ghost", false);
                ghost.kids.push(textNode(cx + 11, y + 27, "g-sub", "no entry"));
                boxes.push(ghost);
            }

            const items = Array.isArray(w.queueItems) && w.queueItems.length === (w.queue || []).length ? w.queueItems : (w.queue || []).map((id) => ({ id }));
            const streak = w.queueStreak || [];
            if (w.queuePaused || items.length) {
                const x1 = cx + NODE_W;
                const x2 = colX(2);
                flows.push(flowArrow(x1, x2, mid, w.queuePaused ? "paused" : ""));
                if (w.queuePaused) extra.push(pauseMark((x1 + x2) / 2, mid, w.queuePaused, streak));
                if (streak.length || w.queuePaused) {
                    const label = textNode((x1 + x2) / 2, mid - 12, `g-streak${streak.length >= STREAK_LIMIT ? " full" : ""}`, `${streak.length}/${STREAK_LIMIT}`);
                    extra.push(n("g", {}, [label, tip(`Entries started from this queue in a row: ${streak.length} of ${STREAK_LIMIT}${streak.length ? ` (${streak.join(", ")})` : ""}; the queue pauses at ${STREAK_LIMIT} until you act`)]));
                }
            }
            const ids = items.map((it) => it.id);
            items.forEach((item, index) => {
                const col = 2 + index;
                cols = Math.max(cols, col);
                const x = colX(col);
                if (index > 0) flows.push(flowArrow(colX(col - 1) + NODE_W, x, mid, w.queuePaused ? "paused" : ""));
                const status = item.status || "queued";
                const blockers = openBlockers(item);
                const hidden = blockers.filter((b) => !ids.includes(b.id) && !lanes.some((o) => (o.entry && o.entry.id === b.id) || (o.queue || []).includes(b.id)));
                const wait = item.wait || null;
                const qbox = box(x, y, NODE_W, `g-node g-entry g-queued st-${slug(status)}${wait ? " blocked" : ""}`, true);
                const sub = `#${index + 1} ${status}${wait ? (hidden.length ? ` · waits ${hidden.map((b) => b.id).join(", ")}` : " · blocked") : item.status ? " · ready" : ""}`;
                qbox.kids.push(textNode(x + 11, y + 18, "g-id", trunc(item.id, NODE_W - 30, ID_CHAR_PX)), textNode(x + 11, y + 34, "g-sub", trunc(sub, NODE_W - 18)));
                qbox.kids.push(tip([
                    `${item.id} (#${index + 1} in the queue of ${w.name}${item.status ? `, ${item.status}` : ""})`,
                    item.title,
                    wait ? `Not started now: ${wait}` : item.status ? "Takeable: the panel starts it when the worker is free and it is first" : "",
                    "Drag left or right to reorder; × takes it out of the queue",
                ].filter(Boolean).join("\n")));
                const x0 = x + NODE_W - 10;
                const y0 = y + 10;
                const close = n("g", { class: "g-x" }, [n("circle", { cx: x0, cy: y0, r: 7 }), textNode(x0, y0 + 3.5, "", "×"), tip(`take ${item.id} out of the queue of ${w.name}`)]);
                close.act = { type: "unqueue", entry: item.id, worker: w.k };
                qbox.kids.push(close);
                qbox.act = { type: "entry", entry: item.id, worker: w.k, name: w.name, held: false };
                qbox.drag = { worker: w.k, index, ids };
                boxes.push(qbox);
                const at = { id: item.id, x, y, w: NODE_W, lane };
                if (!pos.has(item.id)) pos.set(item.id, at);
                blocked.push({ at, blockers });
            });
        });
        const deps = [];
        blocked.forEach(({ at, blockers }) => {
            blockers.forEach((b) => {
                const from = pos.get(b.id);
                if (from && from !== at) deps.push(depArrow(from, at, b));
            });
        });
        const width = colX(cols) + NODE_W + PAD;
        const height = laneY(lanes.length) - GAP_Y + PAD + 4;
        const tree = n("svg", { xmlns: SVG_NS, class: "g-svg", width, height, viewBox: `0 0 ${width} ${height}`, role: "img" }, [
            defs(),
            n("g", { class: "g-flows" }, flows),
            n("g", { class: "g-boxes" }, boxes),
            n("g", { class: "g-deps" }, deps),
            n("g", { class: "g-extra" }, extra),
        ]);
        return { tree, width, height, lanes: lanes.length };
    }

    // --- the SVG as a string (node: a sample render) and as DOM (the webviews) ------------------------------------
    function escapeXml(s) {
        return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
    }

    function toString(node, css) {
        const attrs = Object.entries(node.attrs).map(([k, v]) => ` ${k}="${escapeXml(v)}"`).join("");
        const style = css && node.tag === "svg" ? `<style>${escapeXml(css)}</style>` : "";
        const inner = (node.text !== undefined ? escapeXml(node.text) : "") + node.kids.map((k) => toString(k)).join("");
        return `<${node.tag}${attrs}>${style}${inner}</${node.tag}>`;
    }

    function toSvg(workers, css) {
        return toString(build(workers).tree, css);
    }

    // opts: onSelect(act) for a click on a box, onUnqueue(entry, worker), onReorder(worker, ids)
    function toDom(node, opts) {
        const elem = document.createElementNS(SVG_NS, node.tag);
        Object.entries(node.attrs).forEach(([k, v]) => {
            if (k !== "xmlns") elem.setAttribute(k, String(v));
        });
        if (node.text !== undefined) elem.textContent = String(node.text);
        node.kids.forEach((k) => elem.appendChild(toDom(k, opts)));
        if (node.act && node.act.type === "unqueue") {
            elem.addEventListener("pointerdown", (event) => event.stopPropagation());
            elem.addEventListener("click", (event) => {
                event.stopPropagation();
                if (opts.onUnqueue) opts.onUnqueue(node.act.entry, node.act.worker);
            });
        } else if (node.act) {
            elem.classList.add("g-clickable");
            if (node.drag && opts.onReorder) dragBehaviour(elem, node, opts);
            else elem.addEventListener("click", () => opts.onSelect && opts.onSelect(node.act));
        }
        return elem;
    }

    // A queued box follows the pointer along its lane; let go, it takes the place it was dropped at. A press that does
    // not move is a click.
    function dragBehaviour(elem, node, opts) {
        const step = NODE_W + GAP_X;
        let start = null;
        let moved = false;
        elem.addEventListener("pointerdown", (event) => {
            if (event.button !== 0) return;
            start = event.clientX;
            moved = false;
            elem.setPointerCapture(event.pointerId);
        });
        elem.addEventListener("pointermove", (event) => {
            if (start === null) return;
            const dx = event.clientX - start;
            if (!moved && Math.abs(dx) < DRAG_PX) return;
            moved = true;
            dragging = true;
            elem.classList.add("g-dragging");
            elem.setAttribute("transform", `translate(${dx} 0)`);
        });
        const finish = (event, cancelled) => {
            if (start === null) return;
            const dx = event.clientX - start;
            start = null;
            dragging = false;
            elem.classList.remove("g-dragging");
            elem.removeAttribute("transform");
            if (cancelled) return;
            if (!moved) {
                if (opts.onSelect) opts.onSelect(node.act);
                return;
            }
            const { worker, index, ids } = node.drag;
            const to = Math.max(0, Math.min(ids.length - 1, index + Math.round(dx / step)));
            if (to === index) return;
            const order = ids.slice();
            const [item] = order.splice(index, 1);
            order.splice(to, 0, item);
            opts.onReorder(worker, order);
        };
        elem.addEventListener("pointerup", (event) => finish(event, false));
        elem.addEventListener("pointercancel", (event) => finish(event, true));
    }

    function legendItem(cls, text) {
        const item = document.createElement("span");
        item.className = "g-legend-item";
        const swatch = document.createElement("span");
        swatch.className = `g-swatch ${cls}`;
        item.append(swatch, document.createTextNode(text));
        return item;
    }

    // The graph in a box that scrolls sideways, and a one-line legend. Null when there is no worker.
    function mount(workers, opts) {
        if (!workers || !workers.length) return null;
        const wrap = document.createElement("div");
        wrap.className = "g-wrap";
        const scroller = document.createElement("div");
        scroller.className = "g-scroll";
        scroller.appendChild(toDom(build(workers).tree, opts || {}));
        const legend = document.createElement("div");
        legend.className = "g-legend";
        legend.append(
            legendItem("flow", "next"),
            legendItem("kind-stacked", "stacked on (cut from its branch in review)"),
            legendItem("kind-waits", "waits for (merge or close first)"),
            legendItem("kind-paired", "paired (engine → game)"),
            legendItem("pause", "queue paused")
        );
        wrap.append(scroller, legend);
        return wrap;
    }

    return { build, toSvg, mount, sessionOf, isDragging: () => dragging };
});
