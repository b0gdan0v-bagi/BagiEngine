"""
A small in-process stand-in for the parts of the Vikunja REST API that the mirror (service/vikunja.py, service/mirror.py) uses.

It copies the behaviour seen on Vikunja 2.7.1 that the adapter depends on:
  - GET /tasks lists done tasks too, with their labels, and pages with X-Pagination-Total-Pages;
  - POST /tasks/{id} replaces the whole task (a field left out falls back to its zero value), but not the labels;
  - labels are set through POST /tasks/{id}/labels/bulk;
  - GET /tasks/{id} answers 404 for a deleted task; project webhooks are listed and created per project;
  - relations: PUT /tasks/{id}/relations adds one and its other side (blocked <-> blocking), 409 when it exists,
    DELETE /tasks/{id}/relations/{kind}/{other} removes both sides; GET /tasks/{id} shows them in
    `related_tasks`; the listing here leaves them out, so the mirror must read a task before changing them.
`fail_next` makes the next N requests answer 500 (the retries of the outbox).
`delay` (seconds) is slept by every request before it answers, outside any lock, so the server threads stay parallel
(a slow Vikunja: the mirror holds its rows for long).
Every request is logged so that a test can assert "no writes".
"""

import json
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

NO_DATE = "0001-01-01T00:00:00Z"
INVERSE_KINDS = {"blocked": "blocking", "blocking": "blocked"}
TASK_DEFAULTS = {"description": "", "done": False, "priority": 0, "due_date": NO_DATE}


class FakeVikunja:
    def __init__(self, token: str = "tk_test"):
        self.token = token
        self.projects: list[dict] = []
        self.labels: dict[int, dict] = {}
        self.tasks: dict[int, dict] = {}
        self.webhooks: dict[int, list[dict]] = {}
        self.relations: set[tuple[int, str, int]] = set()  # (task, kind, other task)
        self.fail_next = 0
        self.delay = 0.0
        self.requests: list[tuple[str, str]] = []
        self._next_id = 1
        fake = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):  # keep the test output quiet
                pass

            def do_GET(self):
                fake._handle(self, "GET")

            def do_PUT(self):
                fake._handle(self, "PUT")

            def do_POST(self):
                fake._handle(self, "POST")

            def do_DELETE(self):
                fake._handle(self, "DELETE")

        self._server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        # a short poll interval keeps shutdown() from waiting half a second per test
        self._thread = threading.Thread(target=lambda: self._server.serve_forever(poll_interval=0.01), daemon=True)

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self._server.server_address[1]}"

    @property
    def writes(self) -> list[tuple[str, str]]:
        return [request for request in self.requests if request[0] != "GET"]

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._server.shutdown()
        self._server.server_close()

    def new_id(self) -> int:
        value, self._next_id = self._next_id, self._next_id + 1
        return value

    def add_project(self, title: str) -> dict:
        project = {"id": self.new_id(), "title": title, "is_archived": False}
        self.projects.append(project)
        return project

    def add_label(self, title: str) -> dict:
        label = {"id": self.new_id(), "title": title}
        self.labels[label["id"]] = label
        return label

    def add_task(self, project_id: int, title: str, **fields) -> dict:
        task = {"id": self.new_id(), "title": title, "project_id": project_id, "labels": [], **TASK_DEFAULTS, **fields}
        self.tasks[task["id"]] = task
        return task

    def related(self, task_id: int, kind: str) -> list[int]:
        return sorted(other for task, k, other in self.relations if task == task_id and k == kind)

    def _with_relations(self, task: dict) -> dict:
        related: dict[str, list[dict]] = {}
        for task_id, kind, other in sorted(self.relations):
            if task_id == task["id"] and other in self.tasks:
                related.setdefault(kind, []).append({"id": other, "title": self.tasks[other]["title"]})
        return {**task, "related_tasks": related}

    def tasks_of(self, project_id: int) -> list[dict]:
        return [task for task in self.tasks.values() if task["project_id"] == project_id]

    # --- request handling ---

    def _handle(self, handler: BaseHTTPRequestHandler, method: str) -> None:
        parsed = urlparse(handler.path)
        self.requests.append((method, parsed.path))
        length = int(handler.headers.get("Content-Length") or 0)
        body = json.loads(handler.rfile.read(length)) if length else None
        if self.delay > 0:
            time.sleep(self.delay)
        if handler.headers.get("Authorization") != f"Bearer {self.token}":
            return self._reply(handler, 401, {"code": 11, "message": "missing or invalid token"})
        if self.fail_next > 0:
            self.fail_next -= 1
            return self._reply(handler, 500, {"message": "internal error (fake)"})
        route = parsed.path.removeprefix("/api/v1")
        query = {key: values[0] for key, values in parse_qs(parsed.query).items()}

        if method == "GET" and route == "/projects":
            needle = query.get("s", "")
            return self._paged(handler, [p for p in self.projects if needle in p["title"]], query)
        if method == "PUT" and route == "/projects":
            return self._reply(handler, 201, self.add_project(body["title"]))
        if method == "GET" and route == "/labels":
            return self._paged(handler, list(self.labels.values()), query)
        if method == "PUT" and route == "/labels":
            return self._reply(handler, 201, self.add_label(body["title"]))
        if method == "GET" and route == "/tasks":
            match = re.fullmatch(r"project_id = (\d+)", query.get("filter", ""))
            tasks = self.tasks_of(int(match.group(1))) if match else list(self.tasks.values())
            return self._paged(handler, tasks, query)

        match = re.fullmatch(r"/projects/(\d+)/tasks", route)
        if method == "PUT" and match:
            fields = {key: value for key, value in body.items() if key in ("description", "done", "priority", "due_date")}
            return self._reply(handler, 201, self.add_task(int(match.group(1)), body["title"], **fields))

        match = re.fullmatch(r"/tasks/(\d+)/labels/bulk", route)
        if method == "POST" and match:
            task = self.tasks[int(match.group(1))]
            task["labels"] = [self.labels[item["id"]] for item in body["labels"]]
            return self._reply(handler, 201, {"labels": body["labels"]})

        match = re.fullmatch(r"/tasks/(\d+)/relations", route)
        if method == "PUT" and match:
            task_id, other, kind = int(match.group(1)), body["other_task_id"], body["relation_kind"]
            if task_id not in self.tasks or other not in self.tasks:
                return self._reply(handler, 404, {"code": 4002, "message": "The task does not exist."})
            if (task_id, kind, other) in self.relations:
                return self._reply(handler, 409, {"code": 4007, "message": "The task relation already exists."})
            self.relations |= {(task_id, kind, other), (other, INVERSE_KINDS.get(kind, kind), task_id)}
            return self._reply(handler, 201, {"task_id": task_id, "other_task_id": other, "relation_kind": kind})
        match = re.fullmatch(r"/tasks/(\d+)/relations/(\w+)/(\d+)", route)
        if method == "DELETE" and match:
            task_id, kind, other = int(match.group(1)), match.group(2), int(match.group(3))
            if (task_id, kind, other) not in self.relations:
                return self._reply(handler, 404, {"code": 4008, "message": "The task relation does not exist."})
            self.relations -= {(task_id, kind, other), (other, INVERSE_KINDS.get(kind, kind), task_id)}
            return self._reply(handler, 200, {"message": "The task relation was successfully deleted."})

        match = re.fullmatch(r"/projects/(\d+)/webhooks", route)
        if match:
            hooks = self.webhooks.setdefault(int(match.group(1)), [])
            if method == "GET":
                return self._reply(handler, 200, hooks)
            if method == "PUT":
                hook = {"id": self.new_id(), "project_id": int(match.group(1)), **body}
                hooks.append(hook)
                return self._reply(handler, 201, hook)

        match = re.fullmatch(r"/tasks/(\d+)", route)
        if match and int(match.group(1)) not in self.tasks:
            return self._reply(handler, 404, {"code": 4002, "message": "The task does not exist."})
        if method == "GET" and match:
            return self._reply(handler, 200, self._with_relations(self.tasks[int(match.group(1))]))
        if method == "DELETE" and match:
            task_id = int(match.group(1))
            del self.tasks[task_id]
            self.relations = {relation for relation in self.relations if task_id not in (relation[0], relation[2])}
            return self._reply(handler, 200, {"message": "Successfully deleted."})
        if method == "POST" and match:
            task = self.tasks[int(match.group(1))]
            replaced = {"id": task["id"], "project_id": body.get("project_id") or task["project_id"], "labels": task["labels"]}
            replaced.update(TASK_DEFAULTS)  # a field left out of the body is reset, as on the real server
            replaced.update({key: value for key, value in body.items() if key not in ("id", "labels", "project_id", "related_tasks")})
            self.tasks[task["id"]] = replaced
            return self._reply(handler, 200, replaced)

        self._reply(handler, 404, {"message": f"no route {method} {route}"})

    def _paged(self, handler, items: list[dict], query: dict) -> None:
        per_page = int(query.get("per_page", 50))
        page = int(query.get("page", 1))
        total_pages = max(1, -(-len(items) // per_page))
        chunk = items[(page - 1) * per_page : page * per_page]
        self._reply(handler, 200, chunk, {"X-Pagination-Total-Pages": str(total_pages)})

    @staticmethod
    def _reply(handler, status: int, payload, headers: dict | None = None) -> None:
        data = json.dumps(payload).encode("utf-8")
        handler.send_response(status)
        handler.send_header("Content-Type", "application/json")
        handler.send_header("Content-Length", str(len(data)))
        for key, value in (headers or {}).items():
            handler.send_header(key, value)
        handler.end_headers()
        handler.wfile.write(data)
