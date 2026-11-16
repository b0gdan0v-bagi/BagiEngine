#!/usr/bin/env python3
"""
The health check of the ComfyUI that art generation uses (tools/artgen/CLAUDE.md). Standard library only.

    python3 tools/artgen/comfyui_check.py [--url http://127.0.0.1:8188] [--json] [--generate] [--timeout 300]

It asks ComfyUI for its version and devices (`/system_stats`) and for the files of every model folder the manifest names
(`/models/<folder>`), and compares both with `comfyui/manifest.json`. `--generate` also queues `workflows/sdxl_basic.json`
(`POST /prompt`) and waits for its image (`/history/<id>`), so the GPU path is proven, not only the server.

Exit code: 0 ready, 1 reachable but not ready (a model missing, the generation failed), 2 unreachable (not started). A version
other than the manifest's is a warning, not a failure: a newer ComfyUI may still run the workflows.

The address: `--url`, else `COMFYUI_URL`, else `comfyuiUrl` of the git-ignored `tools/artgen/local.json`, else the local default.
"""

import argparse
import json
import os
import random
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ARTGEN_DIR = Path(__file__).resolve().parent
MANIFEST = ARTGEN_DIR / "comfyui" / "manifest.json"
WORKFLOW = ARTGEN_DIR / "workflows" / "sdxl_basic.json"
LOCAL_SETTINGS = ARTGEN_DIR / "local.json"
DEFAULT_URL = "http://127.0.0.1:8188"
REQUEST_TIMEOUT_S = 10.0
POLL_INTERVAL_S = 0.5

EXIT_READY = 0
EXIT_NOT_READY = 1
EXIT_UNREACHABLE = 2


class Unreachable(Exception):
    """ComfyUI did not answer at all (not started, wrong address)."""


def resolve_url(cli_url, env=None, settings_path=LOCAL_SETTINGS):
    env = os.environ if env is None else env
    if cli_url:
        return cli_url.rstrip("/")
    if env.get("COMFYUI_URL"):
        return env["COMFYUI_URL"].rstrip("/")
    if settings_path.is_file():
        url = json.loads(settings_path.read_text(encoding="utf-8")).get("comfyuiUrl")
        if url:
            return url.rstrip("/")
    return DEFAULT_URL


def load_manifest(path=MANIFEST):
    return json.loads(path.read_text(encoding="utf-8"))


def _request(url, data=None):
    body = None if data is None else json.dumps(data).encode("utf-8")
    headers = {} if body is None else {"Content-Type": "application/json"}
    request = urllib.request.Request(url, data=body, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT_S) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        # An HTTP error is an answer: the server runs. ComfyUI explains a refused prompt in the body.
        detail = error.read().decode("utf-8", "replace")
        raise RuntimeError(f"{url}: HTTP {error.code}: {detail[:500]}") from error
    except (urllib.error.URLError, ConnectionError, TimeoutError) as error:
        raise Unreachable(f"{url}: {getattr(error, 'reason', error)}") from error


def check(url, manifest):
    """The report as a dict; `ready` is true when the server runs and every model of the manifest is there."""
    report = {"url": url, "reachable": False, "ready": False, "problems": [], "warnings": []}
    try:
        stats = _request(f"{url}/system_stats")
    except Unreachable as error:
        report["problems"].append(f"ComfyUI does not answer ({error}); start it with start-local.bat")
        return report
    report["reachable"] = True
    version = stats.get("system", {}).get("comfyui_version")
    report["version"] = version
    report["devices"] = [
        {"name": d.get("name"), "type": d.get("type"), "vramTotal": d.get("vram_total"), "vramFree": d.get("vram_free")}
        for d in stats.get("devices", [])
    ]
    expected = manifest["comfyui"]["version"]
    if version != expected:
        report["warnings"].append(f"ComfyUI {version}, the manifest pins {expected}")
    if not any(d["type"] == "cuda" for d in report["devices"]):
        report["warnings"].append("no CUDA device: ComfyUI runs on the CPU (the wrong portable build?)")

    present = {}
    for folder in sorted({m["folder"] for m in manifest["models"]}):
        try:
            present[folder] = set(_request(f"{url}/models/{folder}"))
        except RuntimeError as error:
            report["problems"].append(str(error))
            present[folder] = set()
    report["models"] = []
    for model in manifest["models"]:
        found = model["name"] in present[model["folder"]]
        report["models"].append({"name": model["name"], "folder": model["folder"], "present": found})
        if not found:
            report["problems"].append(f"model {model['folder']}/{model['name']} is missing: run comfyui/install.ps1")
    report["ready"] = not report["problems"]
    return report


def with_fresh_seed(workflow, rng=random):
    """A copy with a new seed in every sampler: ComfyUI caches node results, so a repeated seed would skip the GPU entirely."""
    fresh = json.loads(json.dumps(workflow))
    for node in fresh.values():
        if "seed" in node.get("inputs", {}):
            node["inputs"]["seed"] = rng.randrange(2**48)
    return fresh


def generate(url, workflow, timeout_s, clock=time.monotonic, sleep=time.sleep):
    """Queues the workflow and waits for it; returns (seconds, output file names). RuntimeError when it fails or times out."""
    started = clock()
    answer = _request(f"{url}/prompt", {"prompt": workflow})
    prompt_id = answer["prompt_id"]
    while clock() - started < timeout_s:
        entry = _request(f"{url}/history/{prompt_id}").get(prompt_id)
        if entry:
            status = entry.get("status", {})
            if status.get("status_str") == "error":
                raise RuntimeError(f"the generation failed: {json.dumps(status.get('messages', []))[:500]}")
            if status.get("completed"):
                files = [image["filename"] for output in entry.get("outputs", {}).values() for image in output.get("images", [])]
                return clock() - started, files
        sleep(POLL_INTERVAL_S)
    raise RuntimeError(f"no image after {timeout_s:.0f} s")


def print_report(report):
    state = "ready" if report["ready"] else ("not ready" if report["reachable"] else "unreachable")
    print(f"ComfyUI at {report['url']}: {state}")
    if report["reachable"]:
        print(f"  version {report.get('version')}")
        for device in report.get("devices", []):
            total = (device["vramTotal"] or 0) / 2**30
            free = (device["vramFree"] or 0) / 2**30
            print(f"  device {device['name']} ({device['type']}), VRAM {free:.1f} of {total:.1f} GiB free")
        for model in report.get("models", []):
            print(f"  {'ok     ' if model['present'] else 'MISSING'} {model['folder']}/{model['name']}")
    for warning in report["warnings"]:
        print(f"  warning: {warning}")
    for problem in report["problems"]:
        print(f"  problem: {problem}")
    if "generation" in report:
        generation = report["generation"]
        if generation.get("error"):
            print(f"  generation failed: {generation['error']}")
        else:
            print(f"  generated {', '.join(generation['files'])} in {generation['seconds']:.1f} s")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--url", help=f"the ComfyUI address (default: COMFYUI_URL, local.json, {DEFAULT_URL})")
    parser.add_argument("--json", action="store_true", help="print the report as JSON")
    parser.add_argument("--generate", action="store_true", help="also generate one image with workflows/sdxl_basic.json")
    parser.add_argument("--timeout", type=float, default=300.0, help="seconds to wait for the image (default 300)")
    args = parser.parse_args(argv)

    url = resolve_url(args.url)
    report = check(url, load_manifest())
    if args.generate and report["ready"]:
        try:
            workflow = with_fresh_seed(json.loads(WORKFLOW.read_text(encoding="utf-8")))
            seconds, files = generate(url, workflow, args.timeout)
            report["generation"] = {"seconds": seconds, "files": files}
        except (RuntimeError, Unreachable) as error:
            report["generation"] = {"error": str(error)}
            report["problems"].append(f"generation: {error}")
            report["ready"] = False

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print_report(report)
    if not report["reachable"]:
        return EXIT_UNREACHABLE
    return EXIT_READY if report["ready"] else EXIT_NOT_READY


if __name__ == "__main__":
    sys.exit(main())
