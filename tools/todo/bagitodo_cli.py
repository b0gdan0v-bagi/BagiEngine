"""
The loader of the todo command line: the code lives in the todo service, a checkout keeps only this file and two
three-line scripts (todo.py, worker.py) at the paths its skills, rules and tools already call.

    python3 tools/todo/todo.py <args>     ->  <cache>/<version>/todo.py <args>
    python3 tools/todo/worker.py <args>   ->  <cache>/<version>/worker.py <args>

Each run asks the service which version it hands out (GET /api/v1/cli: names and sha256 of the files), downloads a
version it has not seen into the cache, checks every hash, and runs it with the same arguments; the answer is trusted
for CHECK_S seconds, so a burst of calls asks once. When the service does not answer, the last version that ran is
used, with a line on stderr; with none yet, an error.

Settings, as the command line itself reads them: TODO_API_URL and TODO_API_TOKEN from the environment, then from
`--env-file`, then from the first `.env` from the current folder upwards (with TODO_API_URL in it). The cache:
BAGITODO_CACHE, default ~/.cache/bagitodo (only its owner can read it: it holds code that runs). Versions other than
the current one are removed after KEEP_DAYS days without a run.

Python 3.10+, standard library only; nothing of the service's address or token is in this file.
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

ENV_URL = "TODO_API_URL"
ENV_TOKEN = "TODO_API_TOKEN"
ENV_CACHE = "BAGITODO_CACHE"
ENV_LAUNCHER = "BAGITODO_LAUNCHER"  # the script the owner called, for the hints the command line prints
TIMEOUT_S = 10
CHECK_S = 60
KEEP_DAYS = 14
CURRENT = "current"  # the file with the version that ran last
VERSION_RE = re.compile(r"[0-9a-f]{8,64}")
NAME_RE = re.compile(r"[A-Za-z0-9_]+\.py")
_ENV_LINE_RE = re.compile(r"^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")


class LoaderError(Exception):
    pass


# --- settings (the same lookup as the command line's) -------------------------------------------


def read_env_file(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for line in path.read_text(encoding="utf-8").splitlines():
        match = _ENV_LINE_RE.match(line)
        if not match:
            continue
        value = match.group(2)
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[match.group(1)] = value
    return values


def env_file_arg(argv: list[str]) -> Path | None:
    for i, arg in enumerate(argv):
        if arg == "--env-file" and i + 1 < len(argv):
            return Path(argv[i + 1]).expanduser()
        if arg.startswith("--env-file="):
            return Path(arg.partition("=")[2]).expanduser()
    return None


def settings(argv: list[str], cwd: Path) -> tuple[str, str]:
    """(url, token); "" for what is not set (the command line then says so itself)."""
    given = env_file_arg(argv)
    values: dict[str, str] = {}
    if given:
        values = read_env_file(given)
    else:
        for folder in [cwd, *cwd.parents]:
            found = read_env_file(folder / ".env")
            if ENV_URL in found:
                values = found
                break
    url = (os.environ.get(ENV_URL) or values.get(ENV_URL) or "").strip().rstrip("/")
    token = (os.environ.get(ENV_TOKEN) or values.get(ENV_TOKEN) or "").strip()
    return url, token


# --- the cache ----------------------------------------------------------------------------------


def cache_root() -> Path:
    root = Path(os.environ.get(ENV_CACHE) or Path.home() / ".cache" / "bagitodo").expanduser()
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    return root


def complete(folder: Path, hashes: dict[str, str]) -> bool:
    return folder.is_dir() and all(
        (folder / name).is_file() and hashlib.sha256((folder / name).read_bytes()).hexdigest() == digest
        for name, digest in hashes.items()
    )


def current(root: Path) -> str | None:
    try:
        version = (root / CURRENT).read_text(encoding="utf-8").strip()
    except OSError:
        return None
    return version if VERSION_RE.fullmatch(version) and (root / version).is_dir() else None


def set_current(root: Path, version: str) -> None:
    fd, tmp = tempfile.mkstemp(dir=root, prefix=".current-")
    with os.fdopen(fd, "w", encoding="utf-8") as out:
        out.write(version + "\n")
    os.replace(tmp, root / CURRENT)  # also refreshes the time of the last check (CHECK_S)


def prune(root: Path, keep: str, now: float) -> None:
    for folder in root.iterdir():
        if folder.is_dir() and VERSION_RE.fullmatch(folder.name) and folder.name != keep:
            if now - folder.stat().st_mtime > KEEP_DAYS * 86400:
                shutil.rmtree(folder, ignore_errors=True)


# --- the service --------------------------------------------------------------------------------


def get(url: str, token: str, path: str) -> bytes:
    request = urllib.request.Request(url + path, headers={"Authorization": f"Bearer {token}", "X-Todo-Actor": "loader"})
    with urllib.request.urlopen(request, timeout=TIMEOUT_S) as response:
        return response.read()


def manifest(url: str, token: str) -> tuple[str, dict[str, str]]:
    data = json.loads(get(url, token, "/api/v1/cli"))
    version, hashes = data.get("version"), data.get("files")
    if not (isinstance(version, str) and VERSION_RE.fullmatch(version) and isinstance(hashes, dict) and hashes):
        raise LoaderError("the service's answer about the command line is not a version and its files")
    if not all(isinstance(n, str) and NAME_RE.fullmatch(n) and isinstance(h, str) for n, h in hashes.items()):
        raise LoaderError("the service named a file that is not a plain .py name")
    return version, hashes


def download(url: str, token: str, root: Path, version: str, hashes: dict[str, str]) -> Path:
    """The version's files into a temporary folder, each hash checked, then renamed into place in one step."""
    target = root / version
    if complete(target, hashes):
        return target
    staging = Path(tempfile.mkdtemp(dir=root, prefix=f".{version}-"))
    try:
        for name, digest in hashes.items():
            data = get(url, token, f"/api/v1/cli/{name}")
            if hashlib.sha256(data).hexdigest() != digest:
                raise LoaderError(f"{name} of version {version} does not match its hash: not used")
            (staging / name).write_bytes(data)
        if target.exists():  # an incomplete copy (a run cut short): replaced
            shutil.rmtree(target, ignore_errors=True)
        try:
            os.replace(staging, target)
        except OSError:  # another run put it there first
            if not complete(target, hashes):
                raise
    finally:
        shutil.rmtree(staging, ignore_errors=True)
    return target


def resolve(argv: list[str], cwd: Path, now: float | None = None, warn=None) -> Path:
    """The folder of the version to run: fresh from the service, or the last one when it does not answer."""
    warn = warn or (lambda line: print(line, file=sys.stderr))
    now = time.time() if now is None else now
    root = cache_root()
    last = current(root)
    try:
        if last and now - (root / CURRENT).stat().st_mtime < CHECK_S:
            return root / last
    except OSError:
        pass
    url, token = settings(argv, cwd)
    try:
        if not (url and token):
            raise LoaderError(f"{ENV_URL} and {ENV_TOKEN} are not both set (environment, --env-file or .env)")
        version, hashes = manifest(url, token)
        folder = download(url, token, root, version, hashes)
    except (LoaderError, urllib.error.URLError, OSError, ValueError) as error:
        reason = f"HTTP {error.code}" if isinstance(error, urllib.error.HTTPError) else str(error)
        if not last:
            raise LoaderError(f"no copy of the todo command line yet, and the service did not hand one out: {reason}") from None
        warn(f"todo loader: the service did not answer ({reason}); running the last copy, version {last}")
        os.utime(root / last)
        return root / last
    set_current(root, version)
    os.utime(folder)
    prune(root, version, now)
    return folder


def run(script: str, launcher: str, argv: list[str] | None = None) -> int:
    """Runs `script` of the version the service hands out with these arguments; never returns on POSIX."""
    argv = sys.argv[1:] if argv is None else argv
    try:
        folder = resolve(argv, Path.cwd())
    except LoaderError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    os.environ[ENV_LAUNCHER] = str(Path(launcher).resolve())
    command = [sys.executable, str(folder / script), *argv]
    if os.name == "posix":
        sys.stdout.flush()
        sys.stderr.flush()
        os.execv(sys.executable, command)
    return subprocess.call(command)
