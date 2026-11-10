"""Dependencies between entries: the store, the API, the graph page, the Vikunja relations and the CLI."""

from datetime import date

import pytest

from service.dashboard import graph_subset, layout_graph, render_graph
from test_cli import cli, server  # noqa: F401  (fixtures)


def add(api, entry_id, area="engine", **fields):
    response = api.post("/api/v1/entries", json={"id": entry_id, "area": area, "title": f"Title of {entry_id}", **fields})
    assert response.status_code == 201, response.text
    return response.json()


def block(api, entry_id, blocker, note=""):
    return api.put(f"/api/v1/entries/{entry_id}/blockers/{blocker}", json={"note": note})


# --- the store and the API ---------------------------------------------------------------------


def test_a_link_shows_on_both_entries_and_blocks_until_the_blocker_is_closed(api):
    add(api, "render-3d")
    add(api, "scene-3d")
    response = block(api, "scene-3d", "render-3d", "meshes need the renderer")
    assert response.status_code == 200, response.text
    scene = response.json()
    assert scene["blocked"] is True
    assert scene["blockedBy"] == [{"id": "render-3d", "status": "open", "title": "Title of render-3d", "note": "meshes need the renderer"}]
    render = api.get("/api/v1/entries/render-3d").json()
    assert render["blocked"] is False and [link["id"] for link in render["blocks"]] == ["scene-3d"]

    api.post("/api/v1/entries/render-3d/close", json={}).raise_for_status()
    scene = api.get("/api/v1/entries/scene-3d").json()
    assert scene["blocked"] is False and scene["blockedBy"][0]["status"] == "closed"


def test_the_ready_and_blocked_filters(api):
    for entry_id in ("a", "b", "c"):
        add(api, entry_id)
    block(api, "b", "a").raise_for_status()
    ids = lambda **query: [entry["id"] for entry in api.get("/api/v1/entries", params=query).json()]  # noqa: E731
    assert ids(blocked="true") == ["b"]
    assert ids(blocked="false") == ["a", "c"]
    assert ids() == ["a", "b", "c"]


def test_a_link_crosses_areas(api):
    api.put("/api/v1/areas/mahjong", json={"title": "Mahjong"}).raise_for_status()
    add(api, "ui-themes")
    add(api, "mahjong-ui-themes", area="mahjong")
    assert block(api, "mahjong-ui-themes", "ui-themes").json()["blocked"] is True


def test_bad_links_are_refused(api):
    add(api, "a")
    add(api, "b")
    add(api, "c")
    assert block(api, "a", "a").status_code == 422
    assert block(api, "a", "missing").status_code == 404
    assert block(api, "missing", "a").status_code == 404
    block(api, "b", "a").raise_for_status()
    block(api, "c", "b").raise_for_status()
    cycle = block(api, "a", "c")
    assert cycle.status_code == 409
    assert "a -> c -> b -> a" in cycle.json()["detail"]
    assert api.delete("/api/v1/entries/a/blockers/b").status_code == 404


def test_adding_twice_changes_the_note_and_removing_unblocks(api):
    add(api, "a")
    add(api, "b")
    block(api, "b", "a", "first").raise_for_status()
    block(api, "b", "a", "second").raise_for_status()
    assert api.get("/api/v1/dependencies").json() == [{"entry": "b", "blocker": "a", "note": "second"}]
    removed = api.delete("/api/v1/entries/b/blockers/a")
    assert removed.status_code == 200 and removed.json()["blockedBy"] == [] and removed.json()["blocked"] is False
    kinds = [event["kind"] for event in api.get("/api/v1/entries/b/events").json()]
    assert kinds == ["unblocked", "blocked by", "blocked by", "create"]


def test_the_graph_page_and_the_entry_page_show_the_links(api):
    add(api, "render-3d", stage=1)
    add(api, "scene-3d")
    add(api, "lonely")
    block(api, "scene-3d", "render-3d").raise_for_status()
    page = api.get("/dashboard/graph")
    assert page.status_code == 200
    assert 'href="entry/render-3d"' in page.text and 'href="entry/scene-3d"' in page.text
    assert "entry/lonely" not in page.text  # an entry without links is not drawn
    assert "scene-3d is blocked by render-3d" in page.text
    entry_page = api.get("/dashboard/entry/scene-3d").text
    assert "Blocked by:" in entry_page and 'href="render-3d"' in entry_page
    dashboard = api.get("/dashboard/").text
    assert "blocked by render-3d" in dashboard and 'href="graph?area=engine"' in dashboard


# --- the graph, pure ---------------------------------------------------------------------------


def entry(entry_id, status="open", area="engine", blocked_by=(), blocks=()):
    links = lambda ids: [{"id": i, "status": s, "title": i, "note": ""} for i, s in ids]  # noqa: E731
    return {
        "id": entry_id,
        "area": area,
        "title": entry_id,
        "status": status,
        "stage": None,
        "blockedBy": links(blocked_by),
        "blocks": links(blocks),
        "blocked": any(s != "closed" for _, s in blocked_by),
    }


def test_layout_puts_a_blocker_left_of_what_it_blocks():
    nodes = ["render", "scene", "picking", "web", "table"]
    edges = [("render", "scene"), ("scene", "picking"), ("render", "web"), ("picking", "table"), ("web", "table"), ("scene", "table")]
    place = layout_graph(nodes, edges)
    column = {node: place[node][0] for node in nodes}
    assert column == {"render": 0, "scene": 1, "web": 1, "picking": 2, "table": 3}
    assert len({place[node] for node in nodes}) == len(nodes)  # no two nodes on one spot


def test_links_between_closed_entries_are_hidden_unless_asked():
    entries = [
        entry("old", "closed", blocks=[("older-child", "closed")]),
        entry("older-child", "closed", blocked_by=[("old", "closed")]),
        entry("base", "closed", blocks=[("next", "open")]),
        entry("next", "open", area="mahjong", blocked_by=[("base", "closed")]),
    ]
    nodes, edges = graph_subset(entries, None, with_closed=False)
    assert edges == [("base", "next")] and set(nodes) == {"base", "next"}
    nodes, edges = graph_subset(entries, None, with_closed=True)
    assert len(edges) == 2
    nodes, edges = graph_subset(entries, "mahjong", with_closed=True)
    assert edges == [("base", "next")]  # an edge with one end in the area keeps the other end


def test_the_graph_page_lists_the_blocked_and_the_ready():
    entries = [
        entry("render-3d", blocks=[("scene-3d", "open")]),
        entry("scene-3d", blocked_by=[("render-3d", "open")]),
    ]
    html = render_graph({"areas": [{"id": "engine", "title": "Engine"}], "area": None, "entries": entries}, date(2026, 11, 10))
    assert "<h2>Blocked</h2>" in html and "waits for" in html
    assert "<h2>Ready, and others wait for them</h2>" in html
    assert 'class="node s-open area-0 ready"' in html


# --- the Vikunja relations ---------------------------------------------------------------------


def task_id(api, entry_id):
    return api.get(f"/api/v1/entries/{entry_id}").json()["vikunjaTaskId"]


def test_a_blocker_becomes_a_blocked_relation_and_goes_away_with_the_link(mirrored):
    api, mirror, fake = mirrored
    add(api, "render-3d")
    add(api, "scene-3d")
    mirror.process_outbox()
    block(api, "scene-3d", "render-3d").raise_for_status()
    mirror.process_outbox()
    scene, render = task_id(api, "scene-3d"), task_id(api, "render-3d")
    assert fake.related(scene, "blocked") == [render]
    assert fake.related(render, "blocking") == [scene]

    mirror.process_outbox()
    before = len(fake.writes)
    api.patch("/api/v1/entries/scene-3d", json={"title": "Scene 3D"}).raise_for_status()
    mirror.process_outbox()
    assert not [w for w in fake.writes[before:] if "/relations" in w[1]]  # a relation that is there is not written again

    api.delete("/api/v1/entries/scene-3d/blockers/render-3d").raise_for_status()
    mirror.process_outbox()
    assert fake.related(scene, "blocked") == [] and fake.related(render, "blocking") == []


def test_a_link_made_before_the_blocker_has_a_task_is_written_when_it_gets_one(mirrored):
    api, mirror, fake = mirrored
    add(api, "scene-3d")
    mirror.process_outbox()
    add(api, "render-3d")
    block(api, "scene-3d", "render-3d").raise_for_status()
    with mirror.pool.connection() as conn:  # the blocked entry is written first, while the blocker has no task
        with conn.transaction():
            mirror.write_entry(conn, "scene-3d")
            conn.execute("DELETE FROM vikunja_outbox WHERE entry_id = 'scene-3d'")
    assert fake.related(task_id(api, "scene-3d"), "blocked") == []
    mirror.process_outbox()  # the blocker's task is made and queues the entry it blocks
    mirror.process_outbox()
    assert fake.related(task_id(api, "scene-3d"), "blocked") == [task_id(api, "render-3d")]


def test_a_relation_to_a_task_outside_the_store_is_kept(mirrored):
    api, mirror, fake = mirrored
    add(api, "scene-3d")
    mirror.process_outbox()
    scene = task_id(api, "scene-3d")
    foreign = fake.add_task(fake.tasks[scene]["project_id"], "made by hand", done=True)
    fake.relations |= {(scene, "blocked", foreign["id"]), (foreign["id"], "blocking", scene)}
    api.patch("/api/v1/entries/scene-3d", json={"title": "Scene 3D"}).raise_for_status()
    mirror.process_outbox()
    assert fake.related(scene, "blocked") == [foreign["id"]]


def test_a_refused_relation_does_not_fail_the_task(mirrored):
    api, mirror, fake = mirrored
    add(api, "render-3d")
    add(api, "scene-3d")
    mirror.process_outbox()
    block(api, "scene-3d", "render-3d").raise_for_status()
    api.patch("/api/v1/entries/scene-3d", json={"title": "Scene 3D"}).raise_for_status()
    original = mirror.client.request

    def refuse_relations(method, path, body=None):
        if "/relations" in path:
            from service.vikunja import VikunjaError

            raise VikunjaError(f"{method} {path} -> HTTP 403: forbidden", 403)
        return original(method, path, body)

    mirror.client.request = refuse_relations
    assert mirror.process_outbox() == 1
    assert fake.tasks[task_id(api, "scene-3d")]["title"] == "Scene 3D"
    assert "relations of scene-3d" in api.get("/healthz").json()["relationsError"]
    mirror.client.request = original
    api.patch("/api/v1/entries/scene-3d", json={"title": "Scene"}).raise_for_status()
    mirror.process_outbox()
    assert api.get("/healthz").json()["relationsError"] is None


# --- the CLI -----------------------------------------------------------------------------------


@pytest.mark.usefixtures("server")
def test_dep_commands_and_list_filters(cli):
    for entry_id in ("render-3d", "scene-3d", "picking-3d"):
        cli("add", entry_id, "--area", "engine", "--title", entry_id)
    out, _ = cli("dep", "add", "picking-3d", "scene-3d", "render-3d", "--note", "needs the scene")
    assert out.strip() == "picking-3d: blocked by render-3d (open: needs the scene), scene-3d (open: needs the scene)"
    cli("dep", "add", "scene-3d", "render-3d")
    _, err = cli("dep", "add", "render-3d", "picking-3d", code=2)
    assert "cycle" in err
    out, _ = cli("dep", "list")
    assert "scene-3d    blocked by  render-3d" in out
    out, _ = cli("dep", "list", "scene-3d")
    assert "blocked by render-3d (open)" in out and "blocks     picking-3d (open: needs the scene)" in out
    out, _ = cli("list", "--ready")
    assert [line.split()[0] for line in out.splitlines()] == ["render-3d"]
    out, _ = cli("list", "--blocked")
    assert "[blocked by render-3d, scene-3d]" in out
    out, _ = cli("show", "scene-3d")
    assert "blocked by: render-3d (open)" in out and "blocks: picking-3d (open: needs the scene)" in out
    cli("dep", "rm", "picking-3d", "render-3d")
    out, _ = cli("dep", "list", "picking-3d")
    assert "render-3d" not in out
    out, _ = cli("dep", "graph", "--area", "engine")
    assert "/dashboard/graph?area=engine" in out
