"""
The loader of the todo command line: the code lives in the todo service, a checkout keeps only this file and two
three-line scripts (todo.py, worker.py) at the paths its skills, rules and tools already call.

    python3 tools/todo/todo.py <args>     ->  <cache>/<version>/todo.py <args>
    python3 tools/todo/worker.py <args>   ->  <cache>/<version>/worker.py <args>

Each run asks the service which version it hands out (GET /api/v1/cli?layout=tree: the relative paths and sha256 of
the files), downloads a version it has not seen into the cache (each file from /api/v1/cli/<path>?layout=tree, into
subfolders where the path has them), checks every hash, and runs it with the same arguments; the answer is trusted
for CHECK_S seconds, so a burst of calls asks once. A path is `[A-Za-z0-9_]+` segments joined by `/`, at most three,
then `.py`: anything else (`..`, an absolute path, a backslash) is refused before a byte is downloaded or written. An
answer without `"layout": "tree"` (an old service, a rollback) is taken as flat as it always was: plain names only, the
files from /api/v1/cli/<name> with no query. When the service does not answer, the last version that ran is
used, with a line on stderr; with none yet, an error.

Two state files name the version that ran last. This loader keeps `current-tree` (the version and, by its mtime,
the time of the last check) and takes both from it alone. The loaders that ask no layout (the flat ones in the
checkouts that were not updated yet) keep `current` and rewrite it every minute with the version of the legacy
bundle; if both loaders shared one file, each would see the other's version as "new" on every run, and a process
that follows the cache would flip between the two bundles. This loader never writes `current`. It reads it only as the last copy to fall back on, when the service does not answer
and `current-tree` is missing, and only when the folder it names is a tree copy (has `.py` files in a subfolder: a
flat copy is no fallback for a loader that runs the package); such a fallback is never "checked just now", so the
service is asked again on the next run, except after a failed check of a new version (below). Old loaders ignore `current-tree`; the prune keeps the versions both files
name.

A version that is new to this cache is tried before it becomes the current one: every file of it is imported in a
fresh interpreter (`-I`, SMOKE_S seconds at most; importing runs no command): a top-level file by its path, a file
under a package folder by its dotted name (todo_worker/queues.py as `todo_worker.queues`, the version's folder first on
sys.path, so its relative imports work). When that fails, the last version keeps
running, one line on stderr names the failure (once per failed version), and the new one is tried again after CHECK_S
(with only a legacy tree copy to fall back on, `smoke-failed` holds that time: its mtime is renewed on every failed
check and, while it is younger than CHECK_S, the copy runs with no question to the service);
with no last version a failed one is an error, never run. A version already current is not tried again.

A pin, BAGITODO_CLI_PIN=<version> (environment or `.env`, as the settings below), runs that version from the cache and
asks the service nothing (no check, no download of a newer one, nothing pruned that it needs). A pinned version that
is not in the cache is downloaded when the service hands out exactly that one, else it is an error: the service keeps
no older versions. Unset the variable to follow the service again.

Settings, as the command line itself reads them: TODO_API_URL, TODO_API_TOKEN (and the pin) from the environment, then from
`--env-file`, then from the first `.env` from the current folder upwards (with TODO_API_URL in it), then, when neither
is set, from the sign-in `todo.py login` saved (TODO_LOGIN_FILE, default ~/.config/bagitodo/login.json).

`todo.py login https://<address>` is run by this file itself, before any download (there is no token to download
with yet): a person the owner approved signs in with Google on the page it prints and allows the code (a device code);
the address and the token go into the login file, readable by its owner only. Everything else, `logout` too, is the
command line's. The cache:
BAGITODO_CACHE, default ~/.cache/bagitodo (only its owner can read it: it holds code that runs). Versions other than
the current one are removed after KEEP_DAYS days without a run.

The loader's own refusals (no settings and no copy, a version that fails its check with no older one, a bad pin) exit
with 1; 2 stays what the command line's argparse says, so a supervisor can tell them apart. A `.env` that cannot be read
is named in the reason of the warning or error (its path and the class of the failure, never its content).

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
ENV_PIN = "BAGITODO_CLI_PIN"
ENV_LOGIN_FILE = "TODO_LOGIN_FILE"
ENV_LAUNCHER = "BAGITODO_LAUNCHER"  # the script the owner called, for the hints the command line prints
TIMEOUT_S = 10
CHECK_S = 60
SMOKE_S = 15
KEEP_DAYS = 14
CURRENT = "current-tree"  # the file with the version that ran last (this loader's own)
LEGACY_CURRENT = "current"  # the same for the loaders that ask no layout: never written here
FAILED = "smoke-failed"  # the version whose check failed and was already told about
VERSION_RE = re.compile(r"[0-9a-f]{8,64}")
TREE = "tree"  # the layout of the answer that has files in folders
NAME_RE = re.compile(r"[A-Za-z0-9_]+\.py")  # a flat answer
PATH_RE = re.compile(r"[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+){0,2}\.py")  # a tree answer: depth 3 at most
_ENV_LINE_RE = re.compile(r"^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")


class LoaderError(Exception):
    pass


# --- settings (the same lookup as the command line's) -------------------------------------------


def read_env_file(path: Path, problems: list[str] | None = None) -> dict[str, str]:
    """The settings of a `.env`; none when it is missing, unreadable or no UTF-8 (the loader then runs the last copy
    and says why, instead of dying before it starts anything). An unreadable file is named in `problems`: its path and
    the class of the error."""
    values: dict[str, str] = {}
    try:
        if not path.is_file():
            return values
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, ValueError) as error:
        if problems is not None:
            problems.append(f"{path} could not be read ({type(error).__name__})")
        return values
    for line in lines:
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


def env_values(argv: list[str], cwd: Path, problems: list[str] | None = None) -> dict[str, str]:
    given = env_file_arg(argv)
    values: dict[str, str] = {}
    if given:
        values = read_env_file(given, problems)
    else:
        for folder in [cwd, *cwd.parents]:
            found = read_env_file(folder / ".env", problems)
            if ENV_URL in found:
                values = found
                break
    return values


def setting(name: str, values: dict[str, str]) -> str:
    return (os.environ.get(name) or values.get(name) or "").strip()


def settings(argv: list[str], cwd: Path, problems: list[str] | None = None) -> tuple[str, str]:
    """(url, token); "" for what is not set (the command line then says so itself). Neither set: the saved sign-in."""
    values = env_values(argv, cwd, problems)
    url, token = setting(ENV_URL, values).rstrip("/"), setting(ENV_TOKEN, values)
    if not url and not token:
        saved = read_login()
        url, token = str(saved.get("url", "")).rstrip("/"), str(saved.get("token", ""))
    return url, token


# --- the sign-in of a person (`todo.py login`; the command line has the same, todo.py `cmd_login`) ------------


def login_file() -> Path:
    if os.environ.get(ENV_LOGIN_FILE):
        return Path(os.environ[ENV_LOGIN_FILE]).expanduser()
    return Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config").expanduser() / "bagitodo" / "login.json"


def read_login() -> dict:
    try:
        saved = json.loads(login_file().read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return saved if isinstance(saved, dict) else {}


def post_json(address: str, body: dict) -> tuple[int, dict]:
    request = urllib.request.Request(address, data=json.dumps(body).encode("utf-8"), method="POST",
                                     headers={"Content-Type": "application/json", "Accept": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT_S) as response:
            return response.status, json.loads(response.read() or b"{}")
    except urllib.error.HTTPError as error:
        try:
            return error.code, json.loads(error.read() or b"{}")
        except ValueError:
            return error.code, {}


def service_address(url: str) -> bool:
    """An address `login` may send a sign-in to: https with a host and no path, or plain http to this machine only."""
    import urllib.parse

    parts = urllib.parse.urlsplit(url.strip().rstrip("/"))
    if parts.path or parts.query or parts.fragment or not parts.hostname:
        return False
    return parts.scheme == "https" or (parts.scheme == "http" and parts.hostname in ("localhost", "127.0.0.1"))


def login(argv: list[str], sleep=time.sleep, now=time.monotonic, open_browser=None) -> int:
    """`todo.py login https://<address> [--no-browser]`: the device code, the page, the poll, the saved token."""
    import socket
    import webbrowser

    urls = [arg for arg in argv if not arg.startswith("-")]
    if len(urls) != 1 or not service_address(urls[0]):
        print("usage: todo.py login https://<the todo address> [--no-browser]", file=sys.stderr)
        return 1
    url = urls[0].rstrip("/")
    try:
        status, started = post_json(f"{url}/api/v1/login/start", {"host": socket.gethostname().split(".")[0]})
        if status != 200:
            raise LoaderError(f"the service refused the sign-in: HTTP {status}: {started.get('detail', '')}")
        print(f"Open {started['url']}")
        print(f"sign in with Google there and allow the code {started['code']} (it expires in {started['expiresIn'] // 60} minutes).")
        if "--no-browser" not in argv:
            (open_browser or webbrowser.open)(started["url"])
        end = now() + started["expiresIn"]
        while now() < end:
            sleep(started.get("interval", 3))
            status, answer = post_json(f"{url}/api/v1/login/poll", {"secret": started["secret"]})
            if status == 200 and answer.get("status") == "pending":
                continue
            if status == 200 and answer.get("token"):
                target = login_file()
                target.parent.mkdir(parents=True, exist_ok=True)
                temporary = target.with_name(f".{target.name}.{os.getpid()}")
                temporary.write_text(json.dumps({"url": url, "token": answer["token"], "name": answer["name"]}) + "\n", encoding="utf-8")
                temporary.chmod(0o600)
                os.replace(temporary, target)
                print(f"signed in as {answer['name']}; the token is in {target} (a .env with {ENV_URL} still comes first)")
                return 0
            raise LoaderError(f"not signed in: {answer.get('detail') or f'HTTP {status}'}")
        raise LoaderError("the code expired before it was allowed: run todo.py login again")
    except (LoaderError, urllib.error.URLError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


# --- the cache ----------------------------------------------------------------------------------


def cache_root() -> Path:
    root = Path(os.environ.get(ENV_CACHE) or Path.home() / ".cache" / "bagitodo").expanduser()
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    return root


def place(folder: Path, name: str) -> Path:
    """Where a manifest path lies in a folder (the manifest's separator is `/` on every system)."""
    return folder.joinpath(*name.split("/"))


def complete(folder: Path, hashes: dict[str, str]) -> bool:
    return folder.is_dir() and all(
        place(folder, name).is_file() and hashlib.sha256(place(folder, name).read_bytes()).hexdigest() == digest
        for name, digest in hashes.items()
    )


def named_version(root: Path, state: str) -> str | None:
    try:
        version = (root / state).read_text(encoding="utf-8").strip()
    except OSError:
        return None
    return version if VERSION_RE.fullmatch(version) and (root / version).is_dir() else None


def current(root: Path) -> str | None:
    return named_version(root, CURRENT)


def legacy_tree_copy(root: Path) -> str | None:
    """The version the flat loaders named, when it is a tree copy that can be run: the fallback of a cache that has
    no `current-tree` yet."""
    version = named_version(root, LEGACY_CURRENT)
    return version if version and any((root / version).glob("*/*.py")) else None


def set_current(root: Path, version: str) -> None:
    fd, tmp = tempfile.mkstemp(dir=root, prefix=".current-")
    with os.fdopen(fd, "w", encoding="utf-8") as out:
        out.write(version + "\n")
    os.replace(tmp, root / CURRENT)  # also refreshes the time of the last check (CHECK_S)


_SMOKE = """
import importlib, importlib.util, sys
sys.dont_write_bytecode = True
folder = sys.argv[1]
sys.path.insert(0, folder)
for name in sys.argv[2:]:
    try:
        if "/" in name:
            dotted = name[:-3].replace("/", ".")
            importlib.import_module(dotted[:-9] if dotted.endswith(".__init__") else dotted)
            continue
        spec = importlib.util.spec_from_file_location("smoke_" + name[:-3], folder + "/" + name)
        spec.loader.exec_module(importlib.util.module_from_spec(spec))
    except BaseException as error:
        sys.exit(f"{name}: {type(error).__name__}: {error}")
"""


def smoke(folder: Path, names) -> str | None:
    """None when every file imports in a fresh interpreter, else why not (the files stay unchanged: no bytecode)."""
    try:
        done = subprocess.run([sys.executable, "-I", "-c", _SMOKE, str(folder), *names], cwd=folder.parent,
                              capture_output=True, text=True, timeout=SMOKE_S)
    except subprocess.TimeoutExpired:
        return f"no answer within {SMOKE_S} s"
    except OSError as error:
        return str(error)
    if done.returncode == 0:
        return None
    lines = (done.stderr or "").strip().splitlines()
    return lines[-1] if lines else f"the interpreter exited with {done.returncode}"


def prune(root: Path, keep: str, now: float) -> None:
    kept = {keep, named_version(root, CURRENT), named_version(root, LEGACY_CURRENT)}
    for folder in root.iterdir():
        if folder.is_dir() and VERSION_RE.fullmatch(folder.name) and folder.name not in kept:
            if now - folder.stat().st_mtime > KEEP_DAYS * 86400:
                shutil.rmtree(folder, ignore_errors=True)


# --- the service --------------------------------------------------------------------------------


def get(url: str, token: str, path: str) -> bytes:
    request = urllib.request.Request(url + path, headers={"Authorization": f"Bearer {token}", "X-Todo-Actor": "loader"})
    with urllib.request.urlopen(request, timeout=TIMEOUT_S) as response:
        return response.read()


def manifest(url: str, token: str) -> tuple[str, dict[str, str], bool]:
    """(version, {path: sha256}, whether the answer is a tree). Every path is checked here, before any download."""
    data = json.loads(get(url, token, f"/api/v1/cli?layout={TREE}"))
    version, hashes, layout = data.get("version"), data.get("files"), data.get("layout")
    if not (isinstance(version, str) and VERSION_RE.fullmatch(version) and isinstance(hashes, dict) and hashes):
        raise LoaderError("the service's answer about the command line is not a version and its files")
    if layout not in (None, TREE):
        raise LoaderError("the service handed out a layout of files this loader does not know")
    tree = layout == TREE
    if not all(isinstance(n, str) and (PATH_RE if tree else NAME_RE).fullmatch(n) and isinstance(h, str) for n, h in hashes.items()):
        raise LoaderError("the service named a file that is not a relative .py path" if tree else "the service named a file that is not a plain .py name")
    return version, hashes, tree


def download(url: str, token: str, root: Path, version: str, hashes: dict[str, str], tree: bool = False) -> Path:
    """The version's files into a temporary folder, each hash checked, then renamed into place in one step."""
    target = root / version
    if complete(target, hashes):
        return target
    staging = Path(tempfile.mkdtemp(dir=root, prefix=f".{version}-"))
    try:
        for name, digest in hashes.items():
            data = get(url, token, f"/api/v1/cli/{name}" + (f"?layout={TREE}" if tree else ""))
            if hashlib.sha256(data).hexdigest() != digest:
                raise LoaderError(f"{name} of version {version} does not match its hash: not used")
            target_file = place(staging, name)
            target_file.parent.mkdir(parents=True, exist_ok=True)
            target_file.write_bytes(data)
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


def pinned(root: Path, pin: str, argv: list[str], cwd: Path) -> Path:
    """The folder of the pinned version: from the cache, else downloaded when the service hands out exactly that one."""
    if not VERSION_RE.fullmatch(pin):
        raise LoaderError(f"{ENV_PIN} is not a version (lower-case hex, 8 to 64 characters): {pin!r}")
    folder = root / pin
    if not (folder.is_dir() and any(folder.glob("*.py"))):
        url, token = settings(argv, cwd)
        try:
            if not (url and token):
                raise LoaderError(f"{ENV_URL} and {ENV_TOKEN} are not both set (environment, --env-file or .env)")
            version, hashes, tree = manifest(url, token)
            if version != pin:
                raise LoaderError(f"the service hands out version {version} only")
            folder = download(url, token, root, version, hashes, tree)
        except (LoaderError, urllib.error.URLError, OSError, ValueError) as error:
            reason = f"HTTP {error.code}" if isinstance(error, urllib.error.HTTPError) else str(error)
            raise LoaderError(f"{ENV_PIN}={pin} is not in the cache and could not be downloaded: {reason}") from None
    os.utime(folder)  # in use: prune keeps it
    return folder


def resolve(argv: list[str], cwd: Path, now: float | None = None, warn=None) -> Path:
    """The folder of the version to run: fresh from the service, or the last one when it does not answer or the
    new one fails its check."""
    warn = warn or (lambda line: print(line, file=sys.stderr))
    now = time.time() if now is None else now
    root = cache_root()
    pin = setting(ENV_PIN, env_values(argv, cwd))
    if pin:
        return pinned(root, pin, argv, cwd)
    last = current(root)
    fallback = last or legacy_tree_copy(root)  # the legacy one is never "checked just now"
    try:
        if last and now - (root / CURRENT).stat().st_mtime < CHECK_S:
            return root / last
        # no `current-tree` to hold the time of a failed check: `smoke-failed` does
        if not last and fallback and 0 <= now - (root / FAILED).stat().st_mtime < CHECK_S:
            os.utime(root / fallback)
            return root / fallback
    except OSError:
        pass
    problems: list[str] = []
    url, token = settings(argv, cwd, problems)
    try:
        if not (url and token):
            raise LoaderError(f"{ENV_URL} and {ENV_TOKEN} are not both set (environment, --env-file or .env)"
                              + "".join(f"; {problem}" for problem in problems))
        version, hashes, tree = manifest(url, token)
        folder = download(url, token, root, version, hashes, tree)
    except (LoaderError, urllib.error.URLError, OSError, ValueError) as error:
        reason = f"HTTP {error.code}" if isinstance(error, urllib.error.HTTPError) else str(error)
        if not fallback:
            raise LoaderError(f"no copy of the todo command line yet, and the service did not hand one out: {reason}") from None
        warn(f"todo loader: the service did not answer ({reason}); running the last copy, version {fallback}")
        os.utime(root / fallback)
        return root / fallback
    if version != last:
        problem = smoke(folder, hashes)
        if problem:
            if not fallback:
                raise LoaderError(f"the service handed out version {version}, which fails its check ({problem}), and there is no older copy")
            try:
                told = (root / FAILED).read_text(encoding="utf-8").strip() == version
            except OSError:
                told = False
            if not told:
                warn(f"todo loader: version {version} fails its check ({problem}); running the last copy, version {fallback}")
            (root / FAILED).write_text(version + "\n", encoding="utf-8")  # also renews its time (see the top of `resolve`)
            os.utime(root / fallback)
            if last:
                os.utime(root / CURRENT)  # the next try of the new version comes after CHECK_S
            return root / fallback
        (root / FAILED).unlink(missing_ok=True)
    set_current(root, version)
    os.utime(folder)
    prune(root, version, now)
    return folder


def run(script: str, launcher: str, argv: list[str] | None = None) -> int:
    """Runs `script` of the version the service hands out with these arguments; never returns on POSIX."""
    argv = sys.argv[1:] if argv is None else argv
    if script == "todo.py" and argv[:1] == ["login"]:
        return login(argv[1:])  # before any download: there is no token yet
    try:
        folder = resolve(argv, Path.cwd())
    except LoaderError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1  # not 2: that is argparse's, and a supervisor tells the two apart
    os.environ[ENV_LAUNCHER] = str(Path(launcher).resolve())
    command = [sys.executable, str(folder / script), *argv]
    if os.name == "posix":
        sys.stdout.flush()
        sys.stderr.flush()
        os.execv(sys.executable, command)
    return subprocess.call(command)
