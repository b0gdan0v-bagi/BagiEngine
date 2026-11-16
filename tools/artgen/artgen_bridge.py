#!/usr/bin/env python3
"""
The generation service's link to the editor (tools/artgen/CLAUDE.md, "The editor bridge"): a client of the editor's control API that
pulls requests of the Generate panel and runs them with artgen_service.

    python3 tools/artgen/artgen_bridge.py [--host 127.0.0.1] [--port 8765] [--generator comfyui|fake] [--url URL]

Start the editor with --control first (or after: the bridge waits and reconnects). Every POLL_S it calls `artgen_poll` with the
generator's status and the game's presets; the answer gives the game root, a new request once and a cancel once. A request runs on a
worker thread; its progress, each image and the end go back as `artgen_progress`, `artgen_image` and `artgen_done`. The contract is
the editor's (src/Widgets/ArtGen/CLAUDE.md in the engine). Ctrl+C stops the bridge and cancels a run in work.
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
from artgen_generators import Cancelled, GenerationError, make_generator  # noqa: E402
from bagi_control_client import DEFAULT_HOST, DEFAULT_PORT, BagiControlClient, ConnectionClosed, ControlError  # noqa: E402

POLL_S = 0.5
BUSY_TICK_S = 0.1
STATUS_REFRESH_S = 5.0
RECONNECT_S = 2.0
# The editor answers with this while the Generate panel has never been created (Game mode, an editor without the panel)
UNKNOWN_METHOD = "unknown_method"


class Bridge:
    """One editor connection. `tick()` is one pass: refresh the status when due, poll, start or cancel a run, forward what the worker
    reported. The client, the generator and the clock are injected, so a test drives it without sockets or a GPU."""

    def __init__(self, client, generator, clock=time.monotonic, log=print):
        self.client = client
        self.generator = generator
        self.clock = clock
        self.log = log
        self.game_root = None
        self._status = None
        self._presets = []
        self._status_at = None
        self._worker = None
        self._run_id = None
        self._cancel = threading.Event()
        self._reports = queue.Queue()

    @property
    def busy(self):
        return self._worker is not None

    def _refresh_status(self):
        status = self.generator.status()
        info = self.generator.info() if status.state == "ready" else {"name": getattr(self.generator, "name", "")}
        self._status = {"state": status.state, "reason": status.reason, "generator": " ".join(str(v) for v in (info.get("name"), info.get("version")) if v)}
        self._presets = []
        if self.game_root:
            for name, preset in artgen_preset.find_presets(self.game_root).items():
                if isinstance(preset, Exception):
                    self._presets.append({"name": name, "title": name, "error": str(preset)})
                else:
                    self._presets.append({"name": name, "title": preset.title})
        self._status_at = self.clock()

    def tick(self):
        """Returns the seconds to wait before the next tick."""
        if self._status_at is None or self.clock() - self._status_at >= STATUS_REFRESH_S:
            self._refresh_status()
        self._forward_reports()
        answer = self.client.call("artgen_poll", {**self._status, "presets": self._presets})
        self.client.events.clear()  # pushes of the game (state_changed) are not ours
        root = answer.get("gameRoot") or None
        if root != self.game_root:
            self.game_root = root
            self._status_at = None  # the presets are the new game's
        request = answer.get("request")
        if request and not self.busy:
            self._start(request)
        if answer.get("cancel") is not None and answer.get("cancel") == self._run_id:
            self._cancel.set()
        self._forward_reports()
        return BUSY_TICK_S if self.busy else POLL_S

    def _start(self, request):
        self._run_id = request["id"]
        self._cancel = threading.Event()
        self.log(f"request {self._run_id}: {request.get('count')} x '{request.get('subject')}' with preset '{request.get('preset')}'")
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
                self._reports.put(("artgen_image", {**report, "path": event["path"], "provenance": provenance}))

        done = {"id": run_id, "status": "ok"}
        try:
            if not self.game_root:
                raise artgen_service.RequestError("the editor gave no game root")
            preset = artgen_preset.load_preset(Path(self.game_root) / "artgen" / "presets" / f"{request['preset']}.json", self.game_root)
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

    def _forward_reports(self):
        while True:
            try:
                method, params = self._reports.get_nowait()
            except queue.Empty:
                return
            self.client.call(method, params)
            if method == "artgen_done":
                self.log(f"request {params['id']}: {params['status']}" + (f": {params['error']}" if params.get("error") else ""))
                self._worker.join()
                self._worker = None
                self._run_id = None

    def stop(self):
        """Cancels a run in work and waits for its worker (Ctrl+C, a lost editor)."""
        if self._worker is not None:
            self._cancel.set()
            self._worker.join()
            self._worker = None
            self._run_id = None
            self._reports = queue.Queue()


def serve(host, port, generator, log=print):
    announced = False
    while True:
        client = BagiControlClient(host, port)
        try:
            client.connect()
        except OSError:
            if not announced:
                log(f"waiting for the editor's control API on {host}:{port} (start the editor with --control)")
                announced = True
            time.sleep(RECONNECT_S)
            continue
        log(f"connected to the editor on {host}:{port}")
        announced = False
        bridge = Bridge(client, generator, log=log)
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
        except (ConnectionClosed, OSError) as error:
            log(f"the editor went away ({error}); reconnecting")
            bridge.stop()
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
    args = parser.parse_args(argv)
    try:
        serve(args.host, args.port, make_generator(args.generator, args.url))
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
