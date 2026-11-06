"""
The todo service: the HTTP API over the store (Postgres), the browser pages, and the Vikunja mirror.

Run: `uvicorn --factory service.app:create_app_from_env` from tools/todo (the Dockerfile does).
Every /api/v1 route needs `Authorization: Bearer <TODO_API_TOKEN>`; the pages take the same token
as the password of HTTP Basic auth (any user name). The webhook of Vikunja is checked by its signature.
`X-Todo-Actor` names who writes (the events table keeps it).
"""

import base64
import hmac
import json
import logging
import os
from contextlib import asynccontextmanager
from dataclasses import dataclass
from datetime import date

from fastapi import Depends, FastAPI, HTTPException, Request
from fastapi.responses import HTMLResponse, JSONResponse, RedirectResponse
from pydantic import BaseModel
from starlette.concurrency import run_in_threadpool

from . import dashboard, db, store
from .mirror import Mirror, verify_signature
from .vikunja import VikunjaClient

log = logging.getLogger("todo.app")

API = "/api/v1"
DEFAULT_ACTOR = "api"


@dataclass
class Settings:
    database_url: str
    api_token: str
    vikunja_url: str = ""
    vikunja_token: str = ""
    webhook_secret: str = ""
    public_url: str = ""
    reconcile_seconds: float = 300.0

    @classmethod
    def from_env(cls) -> "Settings":
        env = os.environ
        public = env.get("TODO_PUBLIC_URL") or (f"https://{env['RAILWAY_PUBLIC_DOMAIN']}" if env.get("RAILWAY_PUBLIC_DOMAIN") else "")
        missing = [name for name in ("DATABASE_URL", "TODO_API_TOKEN") if not env.get(name)]
        if missing:
            raise SystemExit(f"{', '.join(missing)} not set")
        return cls(
            database_url=env["DATABASE_URL"],
            api_token=env["TODO_API_TOKEN"],
            vikunja_url=env.get("VIKUNJA_URL", ""),
            vikunja_token=env.get("VIKUNJA_TOKEN", ""),
            webhook_secret=env.get("VIKUNJA_WEBHOOK_SECRET", ""),
            public_url=public,
            reconcile_seconds=float(env.get("TODO_RECONCILE_SECONDS", "300")),
        )


# --- request bodies ----------------------------------------------------------------------------


class AreaIn(BaseModel):
    title: str
    vikunjaProject: str | None = None
    stages: list[dict] | None = None


class EntryIn(BaseModel):
    id: str
    area: str
    title: str
    body: str = ""
    status: str = "open"
    statusDate: date | None = None
    statusNote: str = ""
    branch: str | None = None
    stage: int | None = None
    plan: str | None = None
    aliases: list[str] = []


class EntryPatch(BaseModel):
    title: str | None = None
    body: str | None = None
    area: str | None = None
    status: str | None = None
    statusDate: date | None = None
    statusNote: str | None = None
    branch: str | None = None
    stage: int | None = None
    plan: str | None = None
    expectedVersion: int | None = None
    clear: list[str] = []  # fields to set to null: "stage", "plan", "branch"


class ClaimIn(BaseModel):
    branch: str


class ReviewIn(BaseModel):
    branch: str
    note: str


class StopIn(BaseModel):
    reason: str


class CloseIn(BaseModel):
    reopen: bool = False


class PlanIn(BaseModel):
    content: str
    entry: str | None = None
    kind: str | None = None


_PATCH_COLUMNS = {
    "title": "title",
    "body": "body",
    "area": "area",
    "status": "status",
    "statusDate": "status_date",
    "statusNote": "status_note",
    "branch": "branch",
    "stage": "stage",
    "plan": "plan",
}
_CLEARABLE = {"stage", "plan", "branch"}


# --- the app -----------------------------------------------------------------------------------


def create_app(settings: Settings, start_mirror: bool = True) -> FastAPI:
    state: dict = {}

    @asynccontextmanager
    async def lifespan(_: FastAPI):
        pool = db.connect_pool(settings.database_url)
        with pool.connection() as conn:
            ran = db.migrate(conn)
            if ran:
                log.info("migrations: %s", ", ".join(ran))
        client = VikunjaClient(settings.vikunja_url, settings.vikunja_token) if settings.vikunja_url and settings.vikunja_token else None
        mirror = Mirror(pool, client, settings.webhook_secret, settings.public_url, settings.reconcile_seconds)
        state.update(pool=pool, mirror=mirror)
        if start_mirror:
            mirror.start()
            if client and settings.public_url and settings.webhook_secret:
                try:
                    for line in mirror.install_webhooks():
                        log.info("webhooks: %s", line)
                except Exception as error:  # Vikunja down at start: the reconcile pass still runs
                    log.warning("webhooks not installed: %s", error)
        yield
        mirror.stop()
        pool.close()

    app = FastAPI(title="todo", lifespan=lifespan, docs_url=None, redoc_url=None)
    app.state.todo = state

    def check_token(request: Request) -> str:
        header = request.headers.get("Authorization") or ""
        token = header.removeprefix("Bearer ").strip() if header.startswith("Bearer ") else ""
        if not token or not hmac.compare_digest(token, settings.api_token):
            raise HTTPException(401, "missing or wrong token", headers={"WWW-Authenticate": "Bearer"})
        return (request.headers.get("X-Todo-Actor") or DEFAULT_ACTOR)[:80]

    def check_page(request: Request) -> None:
        header = request.headers.get("Authorization") or ""
        token = ""
        if header.startswith("Basic "):
            try:
                token = base64.b64decode(header[6:]).decode("utf-8").partition(":")[2]
            except ValueError:
                token = ""
        elif header.startswith("Bearer "):
            token = header[7:].strip()
        if not token or not hmac.compare_digest(token, settings.api_token):
            raise HTTPException(401, "log in with the API token as the password", headers={"WWW-Authenticate": 'Basic realm="todo"'})

    def write(fn):
        """Run fn(conn) in one transaction; StoreError becomes its HTTP status; the mirror is woken after."""
        try:
            with state["pool"].connection() as conn, conn.transaction():
                result = fn(conn)
        except store.StoreError as error:
            raise HTTPException(error.code, str(error)) from None
        state["mirror"].wake()
        return result

    def read(fn):
        try:
            with state["pool"].connection() as conn:
                return fn(conn)
        except store.StoreError as error:
            raise HTTPException(error.code, str(error)) from None

    # --- health ---

    @app.get("/healthz")
    def healthz():
        def check(conn):
            outbox = conn.execute("SELECT count(*) AS n, count(last_error) AS failing FROM vikunja_outbox").fetchone()
            return outbox

        outbox = read(check)
        mirror = state["mirror"]
        return {"ok": True, "mirror": mirror.enabled, "mirrorError": mirror.last_error, "outbox": outbox["n"], "outboxFailing": outbox["failing"]}

    # --- areas ---

    @app.get(f"{API}/areas")
    def get_areas(actor: str = Depends(check_token)):
        return read(store.list_areas)

    @app.put(f"{API}/areas/{{area_id}}")
    def put_area(area_id: str, body: AreaIn, actor: str = Depends(check_token)):
        return write(lambda conn: store.put_area(conn, area_id, body.title, body.vikunjaProject, body.stages, actor))

    # --- entries ---

    @app.get(f"{API}/entries")
    def get_entries(area: str | None = None, status: str | None = None, body: bool = False, actor: str = Depends(check_token)):
        statuses = [part.strip() for part in status.split(",")] if status else None
        return read(lambda conn: store.entries_json(conn, store.list_entries(conn, area, statuses), with_body=body))

    @app.get(f"{API}/entries/{{entry_id}}")
    def get_entry(entry_id: str, actor: str = Depends(check_token)):
        return read(lambda conn: store.entry_json(conn, store.require_entry(conn, entry_id)))

    @app.post(f"{API}/entries", status_code=201)
    def post_entry(body: EntryIn, actor: str = Depends(check_token)):
        data = {_PATCH_COLUMNS.get(key, key): value for key, value in body.model_dump().items()}
        return write(lambda conn: store.entry_json(conn, store.create_entry(conn, data, actor)))

    @app.patch(f"{API}/entries/{{entry_id}}")
    def patch_entry(entry_id: str, body: EntryPatch, actor: str = Depends(check_token)):
        given = body.model_dump(exclude_unset=True)
        changes = {_PATCH_COLUMNS[key]: value for key, value in given.items() if key in _PATCH_COLUMNS and value is not None}
        bad = set(body.clear) - _CLEARABLE
        if bad:
            raise HTTPException(422, f"cannot clear {', '.join(sorted(bad))}")
        changes.update({key: None for key in body.clear})
        return write(lambda conn: store.entry_json(conn, store.update_entry(conn, entry_id, changes, actor, expected_version=body.expectedVersion)))

    @app.post(f"{API}/entries/{{entry_id}}/claim")
    def claim(entry_id: str, body: ClaimIn, actor: str = Depends(check_token)):
        return write(lambda conn: store.entry_json(conn, store.claim(conn, entry_id, body.branch, actor)))

    @app.post(f"{API}/entries/{{entry_id}}/review")
    def review(entry_id: str, body: ReviewIn, actor: str = Depends(check_token)):
        return write(lambda conn: store.entry_json(conn, store.review(conn, entry_id, body.branch, body.note, actor)))

    @app.post(f"{API}/entries/{{entry_id}}/stop")
    def stop(entry_id: str, body: StopIn, actor: str = Depends(check_token)):
        return write(lambda conn: store.entry_json(conn, store.stop(conn, entry_id, body.reason, actor)))

    @app.post(f"{API}/entries/{{entry_id}}/close")
    def close(entry_id: str, body: CloseIn, actor: str = Depends(check_token)):
        return write(lambda conn: store.entry_json(conn, store.close(conn, entry_id, body.reopen, actor)))

    @app.get(f"{API}/entries/{{entry_id}}/events")
    def events(entry_id: str, actor: str = Depends(check_token)):
        def run(conn):
            store.require_entry(conn, entry_id)
            return store.events_of(conn, entry_id)

        return read(run)

    # --- plans ---

    @app.get(f"{API}/plans")
    def get_plans(entry: str | None = None, kind: str | None = None, actor: str = Depends(check_token)):
        return read(lambda conn: [store.plan_json(plan, with_content=False) for plan in store.list_plans(conn, entry, kind)])

    @app.get(f"{API}/plans/{{name}}")
    def get_plan(name: str, actor: str = Depends(check_token)):
        def run(conn):
            plan = store.get_plan(conn, name)
            if plan is None:
                raise store.StoreError(404, f"no plan '{name}'")
            return store.plan_json(plan)

        return read(run)

    @app.put(f"{API}/plans/{{name}}")
    def put_plan(name: str, body: PlanIn, actor: str = Depends(check_token)):
        return write(lambda conn: store.plan_json(store.put_plan(conn, name, body.content, body.entry, body.kind, actor)))

    @app.delete(f"{API}/plans/{{name}}", status_code=204)
    def delete_plan(name: str, actor: str = Depends(check_token)):
        write(lambda conn: store.delete_plan(conn, name, actor))

    # --- Vikunja ---

    @app.post(f"{API}/vikunja/webhook")
    async def webhook(request: Request):
        raw = await request.body()
        mirror: Mirror = state["mirror"]
        if not mirror.enabled:
            return JSONResponse({"result": "mirror off"}, status_code=202)
        if not settings.webhook_secret or not verify_signature(settings.webhook_secret, raw, request.headers.get("X-Vikunja-Signature")):
            raise HTTPException(401, "bad signature")
        try:
            payload = json.loads(raw)
        except json.JSONDecodeError:
            raise HTTPException(400, "not JSON") from None
        result = await run_in_threadpool(mirror.on_webhook, payload)
        return {"result": result}

    @app.post(f"{API}/admin/vikunja/reconcile")
    def reconcile(actor: str = Depends(check_token)):
        mirror: Mirror = state["mirror"]
        if not mirror.enabled:
            raise HTTPException(409, "the mirror is off: VIKUNJA_URL and VIKUNJA_TOKEN are not set")
        report = mirror.reconcile()
        written = mirror.process_outbox()
        return {"tasks": report, "written": written}

    @app.post(f"{API}/admin/vikunja/dedupe")
    def dedupe(apply: bool = False, actor: str = Depends(check_token)):
        mirror: Mirror = state["mirror"]
        if not mirror.enabled:
            raise HTTPException(409, "the mirror is off: VIKUNJA_URL and VIKUNJA_TOKEN are not set")
        return {"lines": mirror.dedupe(apply)}

    @app.post(f"{API}/admin/vikunja/webhooks")
    def webhooks(actor: str = Depends(check_token)):
        return {"lines": state["mirror"].install_webhooks()}

    @app.get(f"{API}/admin/outbox")
    def outbox(actor: str = Depends(check_token)):
        return read(lambda conn: conn.execute("SELECT entry_id, due_at, attempts, last_error FROM vikunja_outbox ORDER BY due_at").fetchall())

    # --- pages ---

    @app.get("/dashboard", include_in_schema=False)
    def dashboard_redirect():
        return RedirectResponse("/dashboard/")

    @app.get("/dashboard/", response_class=HTMLResponse)
    def dashboard_page(area: str | None = None, _: None = Depends(check_page)):
        def run(conn):
            areas = store.list_areas(conn)
            current = area or (areas[0]["id"] if areas else None)
            stages = next((a["stages"] for a in areas if a["id"] == current), [])
            entries = store.entries_json(conn, store.list_entries(conn, current, ["open", "in progress", "review", "deferred"]))
            return dashboard.render_dashboard({"areas": areas, "area": current, "stages": stages, "entries": entries}, date.today())

        return read(run)

    @app.get("/dashboard/entry/{entry_id}", response_class=HTMLResponse)
    def entry_page(entry_id: str, _: None = Depends(check_page)):
        def run(conn):
            entry = store.entry_json(conn, store.require_entry(conn, entry_id))
            plans = [store.plan_json(plan, with_content=False) for plan in store.list_plans(conn, entry_id)]
            return dashboard.render_entry(entry, plans, store.events_of(conn, entry_id))

        return read(run)

    @app.get("/dashboard/plan/{name}", response_class=HTMLResponse)
    def plan_page(name: str, _: None = Depends(check_page)):
        def run(conn):
            plan = store.get_plan(conn, name)
            if plan is None:
                raise store.StoreError(404, f"no plan '{name}'")
            return dashboard.render_plan(store.plan_json(plan))

        return read(run)

    return app


def create_app_from_env() -> FastAPI:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    return create_app(Settings.from_env())
