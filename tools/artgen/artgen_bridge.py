#!/usr/bin/env python3
"""
The generation service's link to the editor (tools/artgen/CLAUDE.md, "The editor bridge"): a client of the editor's control API that
pulls requests of the Generate panel and runs them with artgen_service.

    python3 tools/artgen/artgen_bridge.py [--host 127.0.0.1] [--port 8765] [--generator comfyui|fake] [--url URL] [--exit-with-editor]

Start the editor with --control first (or after: the bridge waits and reconnects). Every POLL_S it calls `artgen_poll` with the
generator's status and the game's presets; the answer gives the game root, a new request once and a cancel once. A request runs on a
worker thread; its progress, each image and the end go back as `artgen_progress`, `artgen_image` and `artgen_done`. A setup action
of the panel (`setup`: Plan, Install, Start, Stop of the local generator, artgen_setup.py) runs the same way and reports with
`artgen_setup_report`. The contract is the editor's (src/Widgets/ArtGen/CLAUDE.md in the engine). Ctrl+C stops the bridge and cancels a
run in work; `--exit-with-editor` (the panel's Start the service) also ends it when the editor goes away.
"""

import argparse
import queue
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))

import artgen_preset  # noqa: E402
import artgen_service  # noqa: E402
import artgen_setup  # noqa: E402
import comfyui_check  # noqa: E402
from artgen_generators import Cancelled, GenerationError, make_generator  # noqa: E402
from bagi_control_client import DEFAULT_HOST, DEFAULT_PORT, BagiControlClient, ConnectionClosed, ControlError  # noqa: E402

POLL_S = 0.5
BUSY_TICK_S = 0.1
STATUS_REFRESH_S = 5.0
RECONNECT_S = 2.0
# How long a bridge started by the editor waits for that editor's control API before it gives up
FIRST_CONNECT_S = 60.0
# The editor answers with this while the Generate panel has never been created (Game mode, an editor without the panel)
UNKNOWN_METHOD = "unknown_method"


class ShutdownRequested(Exception):
    """The editor's panel asked the bridge to exit (Disconnect)."""


def load_request_preset(request, game_root):
    """The preset of a request: a name (the game's file, else the engine's starter) or the form's content inline, checked by the
    same rules as a file; a draft is named `draft` in the provenance."""
    preset = request.get("preset")
    if isinstance(preset, dict):
        label = f"the draft of '{request.get('presetName')}'" if request.get("presetName") else "the draft"
        return artgen_preset.parse_preset(preset, artgen_preset.DRAFT, game_root, label=label, source=artgen_preset.DRAFT)
    if isinstance(preset, str):
        return artgen_preset.find_preset(preset, game_root)
    raise artgen_service.RequestError("the request names no preset")


class Bridge:
    """One editor connection. `tick()` is one pass: refresh the status when due, poll, start or cancel a run, forward what the worker
    reported. The client, the generator and the clock are injected, so a test drives it without sockets or a GPU."""

    def __init__(self, client, generator, clock=time.monotonic, log=print, setup=None):
        self.client = client
        self.generator = generator
        self.setup = setup  # artgen_setup.LocalSetup or a fake; None refuses every setup action
        self.clock = clock
        self.log = log
        self.game_root = None
        self._status = None
        self._presets = []
        self._workflows = []
        self._choices = None  # the generator's combo values, asked once it is ready
        self._status_at = None
        self._worker = None
        self._run_id = None
        self._cancel = threading.Event()
        self._reports = queue.Queue()
        self._setup_worker = None
        self._setup_id = None
        self._setup_cancel = threading.Event()

    @property
    def busy(self):
        return self._worker is not None or self._setup_worker is not None

    def _refresh_status(self):
        status = self.generator.status()
        info = self.generator.info() if status.state == "ready" else {"name": getattr(self.generator, "name", "")}
        self._status = {"state": status.state, "reason": status.reason, "generator": " ".join(str(v) for v in (info.get("name"), info.get("version")) if v)}
        # Whether the panel's Stop can end it: only a ComfyUI started by the setup actions, never one started by hand
        self._status["startedHere"] = bool(self.setup is not None and status.state == "ready" and self.setup.started_here())
        if status.state != "ready":
            self._choices = None
        elif self._choices is None:
            self._choices = self.generator.choices()
        self._presets = []
        self._workflows = []
        if self.game_root:
            for name, preset in artgen_preset.find_presets(self.game_root).items():
                if isinstance(preset, Exception):
                    entry = {"name": name, "title": name, "source": getattr(preset, "source", "game"), "error": str(preset)}
                    if isinstance(getattr(preset, "data", None), dict):
                        entry["preset"] = preset.data
                    self._presets.append(entry)
                else:
                    self._presets.append({"name": name, "title": preset.title, "source": preset.source, "kind": preset.kind, "preset": preset.data})
            for name, workflow in artgen_preset.find_workflows(self.game_root).items():
                if isinstance(workflow, Exception):
                    self._workflows.append({"name": name, "error": str(workflow)})
                else:
                    self._workflows.append(workflow.describe(self._choices))
        self._status_at = self.clock()

    def tick(self):
        """Returns the seconds to wait before the next tick."""
        if self._status_at is None or self.clock() - self._status_at >= STATUS_REFRESH_S:
            self._refresh_status()
        self._forward_reports()
        answer = self.client.call("artgen_poll", {**self._status, "presets": self._presets, "workflows": self._workflows})
        self.client.events.clear()  # pushes of the game (state_changed) are not ours
        if answer.get("shutdown"):
            self.stop()
            raise ShutdownRequested()
        if answer.get("refresh"):
            self._status_at = None  # the editor saved a preset: look at the files again on the next tick
        root = answer.get("gameRoot") or None
        if root != self.game_root:
            self.game_root = root
            self._status_at = None  # the presets are the new game's
        request = answer.get("request")
        if request and not self.busy:
            self._start(request)
        if answer.get("cancel") is not None and answer.get("cancel") == self._run_id:
            self._cancel.set()
        setup = answer.get("setup")
        if setup and self._setup_worker is None:
            self._start_setup(setup)
        if answer.get("setupCancel") is not None and answer.get("setupCancel") == self._setup_id:
            self._setup_cancel.set()
        self._forward_reports()
        return BUSY_TICK_S if self.busy else POLL_S

    def _start(self, request):
        self._run_id = request["id"]
        self._cancel = threading.Event()
        preset = request.get("preset")
        named = preset if isinstance(preset, str) else f"a draft of '{request.get('presetName') or artgen_preset.DRAFT}'"
        self.log(f"request {self._run_id}: {request.get('count')} x '{request.get('subject')}' with preset {named}")
        self._worker = threading.Thread(target=self._run, args=(request, self._cancel), daemon=True)
        self._worker.start()

    def _run(self, request, cancel):
        run_id = request["id"]

        def progress(event):
            report = {"id": run_id, "image": event["image"], "count": event["count"], "seed": event["seed"]}
            if event["kind"] == "step":
                self._reports.put(("artgen_progress", {**report, "step": event["step"], "steps": event["steps"]}))
            else:
                provenance = str(Path(event["path"]).with_name(Path(event["path"]).stem + artgen_service.PROVENANCE_SUFFIX))
                self._reports.put(("artgen_image", {**report, "path": event["path"], "provenance": provenance, "kind": event.get("output", "image")}))

        done = {"id": run_id, "status": "ok"}
        try:
            if not self.game_root:
                raise artgen_service.RequestError("the editor gave no game root")
            preset = load_request_preset(request, self.game_root)
            service_request = artgen_service.Request(
                preset, request.get("subject", ""), int(request.get("count") or 1), request.get("seed"), Path(self.game_root) / "artgen" / "out"
            )
            artgen_service.generate(service_request, self.generator, progress, cancel)
        except Cancelled:
            done = {"id": run_id, "status": "cancelled"}
        except (artgen_preset.PresetError, artgen_service.RequestError, GenerationError) as error:
            done = {"id": run_id, "status": "failed", "error": str(error)}
        except Exception as error:  # a bug of ours must still end the run in the editor
            done = {"id": run_id, "status": "failed", "error": f"{type(error).__name__}: {error}"}
        self._reports.put(("artgen_done", done))

    def _start_setup(self, setup):
        self._setup_id = setup["id"]
        self._setup_cancel = threading.Event()
        self.log(f"setup {self._setup_id}: {setup.get('action')}")
        self._setup_worker = threading.Thread(target=self._run_setup, args=(setup, self._setup_cancel), daemon=True)
        self._setup_worker.start()

    def _run_setup(self, setup, cancel):
        setup_id = setup["id"]

        def emit(event):
            if event.get("event") in ("plan", "step", "progress", "log"):
                self._reports.put(("artgen_setup_report", {**event, "id": setup_id}))

        done = {"id": setup_id, "event": "done", "ok": True}
        try:
            if self.setup is None:
                raise artgen_setup.SetupError("this service has no local generator to set up (--generator fake?)")
            self.setup.run(setup.get("action"), emit, cancel)
        except artgen_setup.SetupError as error:
            done = {"id": setup_id, "event": "done", "ok": False, "error": str(error)}
        except Exception as error:  # a bug of ours must still end the action in the editor
            done = {"id": setup_id, "event": "done", "ok": False, "error": f"{type(error).__name__}: {error}"}
        self._reports.put(("artgen_setup_report", done))

    def _forward_reports(self):
        while True:
            try:
                method, params = self._reports.get_nowait()
            except queue.Empty:
                return
            self.client.call(method, params)
            if method == "artgen_setup_report" and params.get("event") == "done":
                self.log(f"setup {params['id']}: " + ("ok" if params["ok"] else f"failed: {params.get('error')}"))
                self._setup_worker.join()
                self._setup_worker = None
                self._setup_id = None
                self._status_at = None  # the generator's state has changed
            if method == "artgen_done":
                self.log(f"request {params['id']}: {params['status']}" + (f": {params['error']}" if params.get("error") else ""))
                self._worker.join()
                self._worker = None
                self._run_id = None

    def stop(self):
        """Cancels a run or an install in work and waits for its worker (Ctrl+C, a lost editor)."""
        if self._setup_worker is not None:
            self._setup_cancel.set()
            self._setup_worker.join()
            self._setup_worker = None
            self._setup_id = None
        if self._worker is not None:
            self._cancel.set()
            self._worker.join()
            self._worker = None
            self._run_id = None
            self._reports = queue.Queue()


def serve(host, port, generator, log=print, setup=None, exit_with_editor=False, clock=time.monotonic):
    announced = False
    waiting_since = clock()
    while True:
        client = BagiControlClient(host, port)
        try:
            client.connect()
        except OSError:
            if exit_with_editor and clock() - waiting_since >= FIRST_CONNECT_S:
                log(f"no editor on {host}:{port}: exiting")
                return
            if not announced:
                log(f"waiting for the editor's control API on {host}:{port} (start the editor with --control)")
                announced = True
            time.sleep(RECONNECT_S)
            continue
        log(f"connected to the editor on {host}:{port}")
        announced = False
        bridge = Bridge(client, generator, log=log, setup=setup)
        try:
            waiting_for_panel = False
            while True:
                try:
                    time.sleep(bridge.tick())
                    waiting_for_panel = False
                except ControlError as error:
                    if error.code != UNKNOWN_METHOD:
                        raise
                    if not waiting_for_panel:
                        log("the editor has no Generate panel yet (Game mode?): waiting")
                        waiting_for_panel = True
                    time.sleep(RECONNECT_S)
        except ShutdownRequested:
            log("the editor asked the service to stop: exiting")
            return
        except (ConnectionClosed, OSError) as error:
            bridge.stop()
            if exit_with_editor:
                log(f"the editor went away ({error}): exiting")
                return
            log(f"the editor went away ({error}); reconnecting")
        except KeyboardInterrupt:
            bridge.stop()
            raise
        finally:
            client.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"the editor's control port (default {DEFAULT_PORT})")
    parser.add_argument("--generator", default="comfyui", choices=["comfyui", "fake"])
    parser.add_argument("--url", help="the ComfyUI address (default: COMFYUI_URL, local.json, the local one)")
    parser.add_argument("--exit-with-editor", action="store_true", help="exit when the editor goes away (the panel started this bridge)")
    parser.add_argument("--log", type=Path, help="append the bridge's lines to this file (the panel starts it without a console)")
    args = parser.parse_args(argv)
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        # pythonw has no stdout at all: everything the bridge and its errors print goes to the file
        sys.stdout = sys.stderr = open(args.log, "a", encoding="utf-8", buffering=1)
        print(f"--- bridge {time.strftime('%Y-%m-%d %H:%M:%S')} for the editor on port {args.port}")
    setup = artgen_setup.LocalSetup(comfyui_check.resolve_url(args.url)) if args.generator == "comfyui" else None
    try:
        serve(args.host, args.port, make_generator(args.generator, args.url), setup=setup, exit_with_editor=args.exit_with_editor)
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
