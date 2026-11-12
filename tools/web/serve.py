"""The static server of the web build, the process the Railway service runs. Standard library only, and this file imports none of the
others of tools/web: the Docker image copies only it.

    python serve.py --root dist                      # PORT from the environment, 8080 without it
    python serve.py --root dist --port 9000 --no-cross-origin-isolation

It serves the folder made by package.py. For a file `x` it answers with `x.br` or `x.gz` when one exists and the request accepts that
encoding (Content-Encoding, Vary: Accept-Encoding, the Content-Length of the encoded body). The names are not hashed (BagiMatch3.wasm
is the same name in every build), so nothing is `immutable` and nothing is reused without asking: every file is `no-cache` (the browser
asks every time, and an ETag makes the answer a 304), so a new deploy reaches the next load, page, .js, .wasm and .data together.
GET /healthz answers 200 `ok` for the health check of the platform.

Cross-origin isolation adds Cross-Origin-Opener-Policy: same-origin and Cross-Origin-Embedder-Policy: require-corp to every answer.
A threaded page (the default web build) needs it: its workers share the wasm memory through a SharedArrayBuffer, which a page gets
only when it is cross-origin isolated. The single-thread page needs neither, and the headers break a page that embeds other origins.
So the server decides, first match wins (the start line says which):
  1. --cross-origin-isolation / --no-cross-origin-isolation
  2. the environment variable COOP_COEP=1 / COOP_COEP=0
  3. `"threads": true|false` in manifest.json of the root (package.py writes it; --single-thread there makes false)
  4. no manifest, or no `threads` in it: on (threaded is the default build)

Access control is optional HTTP Basic: with BASIC_AUTH_USER and BASIC_AUTH_PASSWORD both set in the environment every request but
GET /healthz needs them (401 and `WWW-Authenticate` otherwise, the isolation headers included); with neither set the server is open;
with one only, it does not start. The secret lives in the variables of the service, never in a file of the image.

Match logs (off unless MATCH_STORE names a folder; behind the same Basic auth when it is set, without it every endpoint below is open
to anybody, post, list, read and delete, and the start line says so; with it, anyone who holds the shared password may read and delete
every log): a page posts a match log piece by piece,
`POST /matches/<id>/<index>` with <id> 32 lower-case hex digits, <index> a decimal number without leading zeros, and the body the
lines of the piece (JSON objects, one per line, each ending in a newline; at most 1 MiB; the first piece starts with the `start` event
whose `version` is inside MATCH_LOG_VERSIONS, `N` or `N-M`, which the game owns). The body is stored as it came in
<MATCH_STORE>/<id>/<index>.jsonl and nothing else is: no address, user agent or time (the mtime of the file is the only time), and the
access log never prints a /matches/ line. The same bytes again are 200, other bytes for a stored piece are 409.
At most 60 posts an hour per client address (429 and Retry-After; the address is the socket peer, or with MATCH_FORWARDED_FOR=1 the last
entry of X-Forwarded-For, which the edge of the platform is expected to append; not checked on Railway), and the pieces together stay within MATCH_STORE_MAX_MB (default 1024; 507).
GET /matches/ lists {"matches": [{"id", "pieces"}]}, GET /matches/<id>/<index>.jsonl returns a piece, DELETE /matches/<id> removes a match;
a match whose newest piece is 90 days old is removed at start and every hour. The variables are MATCH_STORE, MATCH_STORE_MAX_MB,
MATCH_LOG_VERSIONS and MATCH_FORWARDED_FOR; without MATCH_STORE the server is as it was.
"""

import argparse
import base64
import binascii
import contextlib
import hmac
import ipaddress
import json
import math
import os
import posixpath
import re
import secrets
import shutil
import sys
import threading
import time
from collections import deque
from email.utils import formatdate
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

DEFAULT_PORT = 8080
# Railway reaches the service from outside the container
DEFAULT_HOST = "0.0.0.0"
INDEX_NAME = "index.html"
MANIFEST_NAME = "manifest.json"
COOP_COEP_VARIABLE = "COOP_COEP"
BASIC_AUTH_USER_VARIABLE = "BASIC_AUTH_USER"
BASIC_AUTH_PASSWORD_VARIABLE = "BASIC_AUTH_PASSWORD"
BASIC_AUTH_REALM = "BagiWeb"
HEALTH_PATH = "/healthz"
NO_CACHE = "no-cache"
COPY_CHUNK = 1 << 20
# Encodings in the order the server prefers them: brotli is smaller than gzip
ENCODINGS = (("br", ".br"), ("gzip", ".gz"))
VARIANT_SUFFIXES = tuple(suffix for _, suffix in ENCODINGS)
STATIC_ALLOW = "GET, HEAD"

MATCH_STORE_VARIABLE = "MATCH_STORE"
MATCH_STORE_MAX_MB_VARIABLE = "MATCH_STORE_MAX_MB"
MATCH_LOG_VERSIONS_VARIABLE = "MATCH_LOG_VERSIONS"
MATCH_FORWARDED_FOR_VARIABLE = "MATCH_FORWARDED_FOR"
MATCH_STORE_MAX_MB_DEFAULT = 1024
MATCH_SEGMENT = "matches"
MATCH_PREFIX = "/" + MATCH_SEGMENT
MATCH_ALLOW = "GET, HEAD, POST, DELETE"
# The ids are made by the game (random 128 bits as hex); a piece number has no leading zero, so a piece has one name only
MATCH_ID_PATTERN = re.compile(r"[0-9a-f]{32}")
MATCH_INDEX_PATTERN = re.compile(r"0|[1-9][0-9]{0,5}")
MATCH_VERSIONS_PATTERN = re.compile(r"([0-9]{1,6})(?:-([0-9]{1,6}))?")
MATCH_BODY_MAX_BYTES = 1 << 20
MATCH_BODY_TIMEOUT_SECONDS = 30
MATCH_TMP_PREFIX = ".tmp-"
MATCH_PIECE_SUFFIX = ".jsonl"
MATCH_CONTENT_TYPE = "application/x-ndjson"
# Posts per client address and window; the sliding window is in memory only
MATCH_RATE_LIMIT = 60
MATCH_RATE_WINDOW_SECONDS = 3600
# Addresses the limiter remembers; beyond it the expired ones go, then the oldest quarter
MATCH_RATE_MAX_ADDRESSES = 10000
# A match whose newest piece is older goes; the sweep runs at start and every hour; a .tmp-* file older than an hour is a crash's leftover
MATCH_RETENTION_SECONDS = 90 * 24 * 3600
MATCH_SWEEP_INTERVAL_SECONDS = 3600
MATCH_TMP_RETENTION_SECONDS = 3600

# Named here, not left to mimetypes: on Windows it reads the registry and has answered text/plain for .js
CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript",
    ".mjs": "text/javascript",
    ".wasm": "application/wasm",
    ".data": "application/octet-stream",
    ".json": "application/json",
    ".webmanifest": "application/manifest+json",
    ".css": "text/css; charset=utf-8",
    ".txt": "text/plain; charset=utf-8",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".webp": "image/webp",
    ".svg": "image/svg+xml",
    ".ico": "image/x-icon",
    ".map": "application/json",
}
DEFAULT_CONTENT_TYPE = "application/octet-stream"


def content_type_of(name: str) -> str:
    return CONTENT_TYPES.get(posixpath.splitext(name)[1].lower(), DEFAULT_CONTENT_TYPE)


def resolve_path(root: Path, url_path: str) -> Path | None:
    """The file below `root` that the URL path names (the path of the request, query cut off already), or None for anything that leaves it (.., a backslash, a NUL, a symlink out).

    The path is cleaned segment by segment before it touches the file system; the final check also catches a link that points out."""
    path = unquote(url_path)
    if "\0" in path or "\\" in path:
        return None
    segments = [segment for segment in path.split("/") if segment not in ("", ".")]
    if any(segment == ".." for segment in segments):
        return None
    candidate = root.joinpath(*segments) if segments else root
    try:
        resolved = candidate.resolve()
    except (OSError, ValueError):
        return None
    if resolved != root and root not in resolved.parents:
        return None
    return resolved


def accepted_encodings(header: str | None) -> set[str]:
    """The content codings an Accept-Encoding header allows (a coding with q=0 is refused; `*` allows every coding we serve)."""
    allowed = set()
    refused = set()
    for item in (header or "").split(","):
        name, _, params = item.strip().partition(";")
        name = name.strip().lower()
        if not name:
            continue
        quality = 1.0
        for param in params.split(";"):
            key, _, value = param.strip().partition("=")
            if key.strip().lower() == "q":
                try:
                    quality = float(value)
                except ValueError:
                    quality = 0.0
        (allowed if quality > 0 else refused).add(name)
    if "*" in allowed:
        allowed |= {coding for coding, _ in ENCODINGS}
    return allowed - refused - {"*"}


def etag_matches(header: str | None, etag: str) -> bool:
    """If-None-Match: a list of tags or `*`; the comparison is the weak one (a W/ prefix is ignored)."""
    if not header:
        return False
    if header.strip() == "*":
        return True
    wanted = etag.removeprefix("W/")
    return any(tag.strip().removeprefix("W/") == wanted for tag in header.split(","))


def _refuse_constant(name: str) -> None:
    raise ValueError(name)


def check_piece(body: bytes, index: int, versions: tuple[int, int] | None) -> str | None:
    """Why a piece is refused (a short fixed text, nothing of the input), None when it is fine.

    The rules: UTF-8; empty, or lines ending in a newline (a line is whatever lies between two "\\n", so a blank line inside is refused);
    every line a JSON object (NaN, Infinity and -Infinity are not JSON and are refused; a CRLF line ending is accepted, the "\\r" is
    whitespace to the parser). An empty piece is fine after the first: the game sends one as the marker of a match given up before any
    settlement. The first piece (index 0) starts with the `start` event whose `version` is an int inside `versions`; with no range
    configured every first piece is refused, because the format is the game's and the server does not guess it."""
    try:
        text = body.decode("utf-8")
    except UnicodeDecodeError:
        return "not UTF-8"
    if not text:
        return "the first piece is empty" if index == 0 else None
    if not text.endswith("\n"):
        return "the last line has no newline"
    first = None
    for number, line in enumerate(text[:-1].split("\n")):
        if not line:
            return "blank line"
        try:
            value = json.loads(line, parse_constant=_refuse_constant)
        except (ValueError, RecursionError):
            return "a line is not JSON"
        if not isinstance(value, dict):
            return "a line is not an object"
        if number == 0:
            first = value
    if index == 0:
        version = first.get("version")
        if first.get("event") != "start" or type(version) is not int:
            return "the first line is not the start event"
        if versions is None or not versions[0] <= version <= versions[1]:
            return "version not accepted"
    return None


def is_piece_name(name: str) -> bool:
    return name.endswith(MATCH_PIECE_SUFFIX) and MATCH_INDEX_PATTERN.fullmatch(name[:-len(MATCH_PIECE_SUFFIX)]) is not None


def sweep_match(folder: Path, now: float, retention: float) -> int:
    """One match folder: old `.tmp-*` files go; the whole folder goes when its newest piece is older than `retention` (with everything
    in it, files of other names too); a folder with nothing in it goes. Returns the bytes of the pieces removed. In a folder that is
    not expired, files of other names are not ours and are left (they keep the folder)."""
    pieces = []
    left = 0
    for item in os.scandir(folder):
        info = item.stat(follow_symlinks=False)
        if not item.is_file(follow_symlinks=False):
            left += 1
        elif item.name.startswith(MATCH_TMP_PREFIX):
            if now - info.st_mtime > MATCH_TMP_RETENTION_SECONDS:
                os.unlink(item.path)
            else:
                left += 1
        elif is_piece_name(item.name):
            pieces.append((info.st_size, info.st_mtime))
        else:
            left += 1
    if pieces and now - max(mtime for _, mtime in pieces) > retention:
        shutil.rmtree(folder)
        return sum(size for size, _ in pieces)
    if not pieces and not left:
        folder.rmdir()
    return 0


def sweep_matches(store: Path, now: float, retention: float = MATCH_RETENTION_SECONDS) -> int:
    """Removes what is out of date from the match store (`now` is a time in seconds like the mtime of a file): matches whose newest
    piece is older than `retention`, `.tmp-*` files older than an hour (also in the folder of the store), empty match folders.
    A folder that does not look like an id is not touched. Returns the bytes of the pieces removed; the caller keeps its counter."""
    freed = 0
    for entry in sorted(os.scandir(store), key=lambda item: item.name):
        try:
            if entry.is_file(follow_symlinks=False) and entry.name.startswith(MATCH_TMP_PREFIX):
                if now - entry.stat(follow_symlinks=False).st_mtime > MATCH_TMP_RETENTION_SECONDS:
                    os.unlink(entry.path)
            elif entry.is_dir(follow_symlinks=False) and MATCH_ID_PATTERN.fullmatch(entry.name):
                freed += sweep_match(Path(entry.path), now, retention)
        except OSError:
            continue  # one folder that cannot be cleaned does not stop the rest
    return freed


class MatchStoreError(Exception):
    """The match store cannot be used (the server then does not start)."""


class MatchStore:
    """The folder of match logs: `<folder>/<id>/<index>.jsonl`, written whole and never changed. One server process owns the folder; a lock
    serialises every decision, write, delete and sweep (a piece is at most 1 MiB and the posts are rate limited, so it is not a
    bottleneck; the sweep holds it during the whole walk, so a post waits for it), and under it `_bytes` (the size of all pieces, counted once at start) stays equal to what is on disk."""

    def __init__(self, folder: Path, versions: tuple[int, int] | None, max_bytes: int):
        self.folder = folder.resolve()
        self.versions = versions
        self.max_bytes = max_bytes
        self._lock = threading.Lock()
        try:
            self.folder.mkdir(parents=True, exist_ok=True)
            probe = self.folder / f"{MATCH_TMP_PREFIX}probe-{secrets.token_hex(4)}"
            probe.write_bytes(b"")
            probe.unlink()
            self._bytes = self._count_bytes()
        except OSError as error:
            raise MatchStoreError(f"match store is not writable: {folder} ({error.strerror or error})") from error

    def _count_bytes(self) -> int:
        total = 0
        for name, _ in self._match_folders():
            for item in os.scandir(self.folder / name):
                if is_piece_name(item.name) and item.is_file(follow_symlinks=False):
                    total += item.stat(follow_symlinks=False).st_size
        return total

    def _match_folders(self) -> list[tuple[str, list[str]]]:
        """(id, names of its pieces) of every folder named like an id, sorted by id; a folder that vanishes meanwhile is skipped."""
        found = []
        for name in sorted(os.listdir(self.folder)):
            if MATCH_ID_PATTERN.fullmatch(name):
                try:
                    found.append((name, [piece for piece in os.listdir(self.folder / name) if is_piece_name(piece)]))
                except OSError:
                    continue
        return found

    def put(self, match_id: str, index: int, body: bytes) -> tuple[HTTPStatus, str]:
        """Stores a checked piece. The first write and a repeat of the same bytes are both 200 (a client that lost the answer posts again
        and must get the same one, and a repeat never counts against the cap); other bytes for a stored piece are 409, the stored file
        stays. A new piece that would take the store over its cap is 507. Two equal posts at once give two 200s: under the lock the
        second one finds the file the first one has finished (the rename is atomic, a reader never sees half a file)."""
        target = self.folder / match_id / f"{index}{MATCH_PIECE_SUFFIX}"
        with self._lock:
            try:
                stored = target.read_bytes()
            except FileNotFoundError:
                if self._bytes + len(body) > self.max_bytes:
                    return HTTPStatus.INSUFFICIENT_STORAGE, "the store is full"
                status = self._write(target, body)
                if status[0] == HTTPStatus.OK:
                    self._bytes += len(body)
                return status
            except OSError:
                return HTTPStatus.INSUFFICIENT_STORAGE, "store failed"
            if stored == body:
                return HTTPStatus.OK, "already stored"
            return HTTPStatus.CONFLICT, "the piece is stored with other bytes"

    def _write(self, target: Path, body: bytes) -> tuple[HTTPStatus, str]:
        directory = target.parent
        temp = directory / f"{MATCH_TMP_PREFIX}{secrets.token_hex(8)}"
        try:
            directory.mkdir(exist_ok=True)
            with open(temp, "xb") as stream:
                stream.write(body)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temp, target)
        except OSError:
            # No half file and no .tmp-* after a failure; rmdir takes only a folder that is empty
            with contextlib.suppress(OSError):
                temp.unlink()
            with contextlib.suppress(OSError):
                directory.rmdir()
            return HTTPStatus.INSUFFICIENT_STORAGE, "store failed"
        return HTTPStatus.OK, "stored"

    def list_matches(self) -> list[dict]:
        """[{"id", "pieces"}] sorted by id; a folder with no piece is not a match."""
        try:
            return [{"id": name, "pieces": len(pieces)} for name, pieces in self._match_folders() if pieces]
        except OSError:
            return []

    def read_piece(self, match_id: str, index: int) -> bytes | None:
        try:
            return (self.folder / match_id / f"{index}{MATCH_PIECE_SUFFIX}").read_bytes()
        except OSError:
            return None

    def delete(self, match_id: str) -> bool | None:
        """Removes a match. True when it was there, False when not, None when the folder could not be removed (the counter is then
        counted again from the disk)."""
        folder = self.folder / match_id
        with self._lock:
            try:
                if not folder.is_dir() or folder.is_symlink():
                    return False
                freed = sum(item.stat().st_size for item in os.scandir(folder) if is_piece_name(item.name))
                shutil.rmtree(folder)
            except OSError:
                with contextlib.suppress(OSError):
                    self._bytes = self._count_bytes()
                return None
            self._bytes -= freed
            return True

    def sweep(self, now: float, retention: float = MATCH_RETENTION_SECONDS):
        with self._lock:
            try:
                self._bytes -= sweep_matches(self.folder, now, retention)
            except OSError:
                pass  # tried again at the next sweep


class RateLimiter:
    """At most `limit` events per key (a client address) in any `window` seconds: a sliding window of the times, in memory only and never
    written. A key's times are purged when it is used; when too many keys are held the expired ones go, then the oldest quarter."""

    def __init__(self, limit: int, window: float, clock, max_keys: int = MATCH_RATE_MAX_ADDRESSES):
        self.limit = limit
        self.window = window
        self.max_keys = max_keys
        self._clock = clock
        self._lock = threading.Lock()
        self._times: dict[str, deque] = {}

    def check(self, key: str) -> int:
        """0 when the event is allowed (and counted), else the whole seconds to wait for the next free place (at least 1)."""
        with self._lock:
            now = self._clock()
            times = self._times.get(key)
            if times is None:
                if len(self._times) >= self.max_keys:
                    self._make_room(now)
                times = self._times[key] = deque()
            while times and times[0] <= now - self.window:
                times.popleft()
            if len(times) >= self.limit:
                return max(1, math.ceil(times[0] + self.window - now))
            times.append(now)
            return 0

    def _make_room(self, now: float):
        for key in [key for key, times in self._times.items() if not times or times[-1] <= now - self.window]:
            del self._times[key]
        if len(self._times) >= self.max_keys:
            for key in list(self._times)[:max(1, self.max_keys // 4)]:
                del self._times[key]


def sweep_loop(store: MatchStore, clock, stop: threading.Event, interval: float = MATCH_SWEEP_INTERVAL_SECONDS):
    """Sweeps the store every `interval` seconds until `stop` is set; wait() returns True then, and is the only sleep."""
    while not stop.wait(interval):
        store.sweep(clock())


class MatchServer(ThreadingHTTPServer):
    """The server; with a match store a daemon thread sweeps it, and server_close() stops that thread."""

    daemon_threads = True
    sweeper: threading.Thread | None = None
    sweeper_stop: threading.Event | None = None

    def start_sweeper(self, store: MatchStore, clock):
        self.sweeper_stop = threading.Event()
        self.sweeper = threading.Thread(target=sweep_loop, args=(store, clock, self.sweeper_stop), name="match-sweep", daemon=True)
        self.sweeper.start()

    def server_close(self):
        if self.sweeper_stop is not None:
            self.sweeper_stop.set()
            self.sweeper.join(timeout=10)
        super().server_close()


class StaticHandler(BaseHTTPRequestHandler):
    """GET and HEAD of the files below `root`, and with a match store the /matches/ endpoint; `root`, `cross_origin_isolation`, `quiet`
    and `match_store` are set by make_server()."""

    root: Path
    cross_origin_isolation = False
    quiet = False
    # b"user:password" a request must present as Basic credentials; None leaves the server open
    basic_credentials: bytes | None = None
    match_store: MatchStore | None = None
    rate_limiter: RateLimiter | None = None
    forwarded_for = False
    protocol_version = "HTTP/1.1"
    server_version = "BagiWebServe"
    # The Server header names the program and not the Python version
    sys_version = ""

    def version_string(self):
        return self.server_version

    def log_message(self, format, *args):
        if self.quiet:
            return
        # The id of a match is the path of its requests; the line of a refused request (send_error) holds the request line too
        if self.match_store is not None and MATCH_PREFIX in format % args:
            return
        super().log_message(format, *args)

    def end_headers(self):
        if self.cross_origin_isolation:
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def do_GET(self):
        self.serve(send_body=True)

    def do_HEAD(self):
        self.serve(send_body=False)

    def authorized(self) -> bool:
        """True when the server is open or the request carries the Basic credentials. The comparison takes the same time whatever
        prefix is right."""
        if self.basic_credentials is None:
            return True
        scheme, _, token = (self.headers.get("Authorization") or "").strip().partition(" ")
        if scheme.lower() != "basic":
            return False
        try:
            presented = base64.b64decode(token.strip(), validate=True)
        except (binascii.Error, ValueError):
            return False
        return hmac.compare_digest(presented, self.basic_credentials)

    def send_unauthorized(self, send_body: bool, close: bool = False):
        body = b"authorization required"
        self.send_response(HTTPStatus.UNAUTHORIZED)
        self.send_header("WWW-Authenticate", f'Basic realm="{BASIC_AUTH_REALM}", charset="UTF-8"')
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        if close:
            self.send_header("Connection", "close")
        self.end_headers()
        if send_body:
            self.wfile.write(body)

    def match_segments(self) -> list[str] | None:
        """The segments after /matches/ of the request path (cut on "/" as they are, nothing decoded: `%2f` is then no separator and
        no hex digit), None for a path that is not below /matches or a server with no match store."""
        if self.match_store is None:
            return None
        parts = urlsplit(self.path).path.split("/")
        return parts[2:] if parts[:2] == ["", MATCH_SEGMENT] else None

    def do_unsupported(self):
        if not self.authorized():
            # The body of a POST is not read, as below
            self.send_unauthorized(send_body=True, close=True)
            return
        # The body of a POST is not read, so on a kept connection it would be parsed as the next request
        self.send_response(HTTPStatus.METHOD_NOT_ALLOWED)
        self.send_header("Allow", STATIC_ALLOW if self.match_segments() is None else MATCH_ALLOW)
        # send_header("Connection", "close") also sets close_connection
        self.send_header("Connection", "close")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def __getattr__(self, name):
        # Every method other than GET and HEAD (TRACE and CONNECT included, and a made-up one) is 405, not the 501 page of the base class
        if name.startswith("do_"):
            return self.do_unsupported
        raise AttributeError(name)

    def do_POST(self):
        """POST /matches/<id>/<index>: one piece of a match log (check_piece(), MatchStore.put()). The answers are 200 (stored, or the same
        bytes again), 400 (path or body), 401, 409 (the piece is stored with other bytes), 411 (no Content-Length, or chunked), 413 (above
        MATCH_BODY_MAX_BYTES, refused before the body is read), 429 (the address posted MATCH_RATE_LIMIT times in the window), 507 (the
        cap is reached, or the store could not write). A refusal that leaves the body unread closes the connection, as a 405 does.
        Anything else is as for any other method."""
        segments = self.match_segments()
        if segments is None:
            self.do_unsupported()
            return
        if not self.authorized():
            self.send_unauthorized(send_body=True, close=True)
            return
        if len(segments) != 2 or not MATCH_ID_PATTERN.fullmatch(segments[0]) or not MATCH_INDEX_PATTERN.fullmatch(segments[1]):
            self.send_text(HTTPStatus.BAD_REQUEST, "bad path", True, close=True)
            return
        # Every post with a good path counts, whatever its body: a flood of bad bodies is limited too
        wait = self.rate_limiter.check(self.client_key())
        if wait:
            self.send_text(HTTPStatus.TOO_MANY_REQUESTS, "too many posts", True, close=True, headers=(("Retry-After", str(wait)),))
            return
        body = self.read_body()
        if body is None:
            return
        index = int(segments[1])
        problem = check_piece(body, index, self.match_store.versions)
        if problem is not None:
            self.send_text(HTTPStatus.BAD_REQUEST, problem, True)
            return
        status, text = self.match_store.put(segments[0], index, body)
        self.send_text(status, text, True)

    def do_DELETE(self):
        """DELETE /matches/<id>: removes the match, 200 when it was there, 404 when not, 400 for a malformed id. Any other path below
        /matches/ (a piece, the list) is 405. The connection closes after the answer: a body sent with it is not read."""
        segments = self.match_segments()
        if segments is None or len(segments) != 1 or not segments[0]:
            self.do_unsupported()
            return
        if not self.authorized():
            self.send_unauthorized(send_body=True, close=True)
            return
        if not MATCH_ID_PATTERN.fullmatch(segments[0]):
            self.send_text(HTTPStatus.BAD_REQUEST, "bad path", True, close=True)
            return
        removed = self.match_store.delete(segments[0])
        if removed is None:
            self.send_text(HTTPStatus.INSUFFICIENT_STORAGE, "store failed", True, close=True)
        elif removed:
            self.send_text(HTTPStatus.OK, "deleted", True, close=True)
        else:
            self.send_text(HTTPStatus.NOT_FOUND, "not found", True, close=True)

    def client_key(self) -> str:
        """The address the rate limit counts: the socket peer, or with MATCH_FORWARDED_FOR=1 the last entry of X-Forwarded-For when it is
        an address (the edge of the platform appends the client; an entry before it is whatever the client wrote, and a client that
        reaches the server directly would dodge the limit if the first one counted)."""
        if self.forwarded_for:
            hops = self.headers.get_all("X-Forwarded-For", [])
            try:
                return str(ipaddress.ip_address(hops[-1].split(",")[-1].strip())) if hops else self.client_address[0]
            except ValueError:
                pass
        return self.client_address[0]

    def serve_match(self, segments: list[str], send_body: bool):
        """GET and HEAD below /matches/: the list `/matches/` and a piece `/matches/<id>/<index>.jsonl`; any other name is 404."""
        store = self.match_store
        if segments == [""]:
            self.send_bytes(json.dumps({"matches": store.list_matches()}).encode("utf-8"), "application/json", send_body)
            return
        body = None
        if len(segments) == 2 and MATCH_ID_PATTERN.fullmatch(segments[0]) and is_piece_name(segments[1]):
            body = store.read_piece(segments[0], int(segments[1][:-len(MATCH_PIECE_SUFFIX)]))
        if body is None:
            self.send_text(HTTPStatus.NOT_FOUND, "not found", send_body)
            return
        self.send_bytes(body, MATCH_CONTENT_TYPE, send_body)

    def read_body(self) -> bytes | None:
        """The body of the request, or None after the refusal was sent (or the client left). Exactly Content-Length bytes are read."""
        if self.headers.get("Transfer-Encoding") is not None:
            self.send_text(HTTPStatus.LENGTH_REQUIRED, "chunked bodies are not taken", True, close=True)
            return None
        lengths = self.headers.get_all("Content-Length", [])
        if not lengths:
            self.send_text(HTTPStatus.LENGTH_REQUIRED, "Content-Length is required", True, close=True)
            return None
        if len(lengths) != 1 or not re.fullmatch(r"[0-9]{1,9}", lengths[0].strip()):
            self.send_text(HTTPStatus.BAD_REQUEST, "bad Content-Length", True, close=True)
            return None
        length = int(lengths[0])
        if length > MATCH_BODY_MAX_BYTES:
            self.send_text(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, "too large", True, close=True)
            return None
        # A client that stops sending must not hold the thread for ever
        self.connection.settimeout(MATCH_BODY_TIMEOUT_SECONDS)
        try:
            body = self.rfile.read(length)
        except OSError:
            body = b""
        self.connection.settimeout(None)
        if len(body) != length:
            self.close_connection = True
            return None
        return body

    def serve(self, send_body: bool):
        url_path = urlsplit(self.path).path
        if url_path == HEALTH_PATH:
            self.send_text(HTTPStatus.OK, "ok", send_body)
            return
        if not self.authorized():
            self.send_unauthorized(send_body)
            return
        segments = self.match_segments()
        if segments is not None:
            self.serve_match(segments, send_body)
            return
        path = resolve_path(self.root, url_path)
        if path is not None and path.is_dir():
            path = path / INDEX_NAME if path == self.root else None
        # name.gz and name.br are variants of name, not files of their own
        if path is None or not path.is_file() or path.name.endswith(VARIANT_SUFFIXES):
            self.send_text(HTTPStatus.NOT_FOUND, "not found", send_body)
            return
        self.send_file(path, send_body)

    def choose_variant(self, path: Path) -> tuple[Path, str | None, bool]:
        """The file to send, its Content-Encoding (None for the plain file) and whether the file has variants at all."""
        allowed = accepted_encodings(self.headers.get("Accept-Encoding"))
        chosen: tuple[Path, str | None] = (path, None)
        has_variants = False
        for coding, suffix in ENCODINGS:
            variant = path.with_name(path.name + suffix)
            if not variant.is_file():
                continue
            # ENCODINGS is in the order of preference: the first variant the request accepts wins
            if chosen[1] is None and coding in allowed:
                chosen = (variant, coding)
            has_variants = True
        return chosen[0], chosen[1], has_variants

    def send_file(self, path: Path, send_body: bool):
        source, coding, has_variants = self.choose_variant(path)
        try:
            stream = source.open("rb")
        except OSError:
            self.send_text(HTTPStatus.NOT_FOUND, "not found", send_body)
            return
        with stream:
            info = os.fstat(stream.fileno())
            # The tag changes with the content and with the encoding chosen, so a cache never mixes the two
            etag = f'"{info.st_mtime_ns:x}-{info.st_size:x}-{coding or "identity"}"'
            headers = [("ETag", etag), ("Cache-Control", NO_CACHE), ("Last-Modified", formatdate(info.st_mtime, usegmt=True))]
            if has_variants:
                headers.append(("Vary", "Accept-Encoding"))
            if etag_matches(self.headers.get("If-None-Match"), etag):
                self.send_response(HTTPStatus.NOT_MODIFIED)
                for name, value in headers:
                    self.send_header(name, value)
                self.end_headers()
                return
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", content_type_of(path.name))
            if coding:
                self.send_header("Content-Encoding", coding)
            self.send_header("Content-Length", str(info.st_size))
            for name, value in headers:
                self.send_header(name, value)
            self.end_headers()
            if send_body:
                try:
                    shutil.copyfileobj(stream, self.wfile, COPY_CHUNK)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    self.close_connection = True

    def send_text(self, status: HTTPStatus, text: str, send_body: bool, close: bool = False, headers: tuple = ()):
        self.send_bytes(text.encode("utf-8"), "text/plain; charset=utf-8", send_body, status, close, headers)

    def send_bytes(self, body: bytes, content_type: str, send_body: bool, status: HTTPStatus = HTTPStatus.OK, close: bool = False,
                   headers: tuple = ()):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for name, value in headers:
            self.send_header(name, value)
        if close:
            self.send_header("Connection", "close")
        self.end_headers()
        if send_body:
            self.wfile.write(body)


def make_server(root: Path, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT, cross_origin_isolation: bool = False,
                quiet: bool = False, basic_auth: tuple[str, str] | None = None, match_store: Path | None = None,
                match_versions: tuple[int, int] | None = None,
                match_store_max_bytes: int = MATCH_STORE_MAX_MB_DEFAULT << 20, match_forwarded_for: bool = False,
                clock=time.time) -> ThreadingHTTPServer:
    """A server for `root`; port 0 takes a free one (server.server_address[1]). `basic_auth` is (user, password) or None for an open
    server. `match_store` is the folder of the /matches/ endpoint (None: the endpoint is off), `match_versions` the (lowest, highest)
    `version` a match log may start with, `match_store_max_bytes` the cap of all pieces, `match_forwarded_for` whether the rate limit
    trusts X-Forwarded-For, `clock` the time of the rate limit and of the sweep (seconds). The folder is created and swept at once;
    MatchStoreError when it cannot be written. The caller runs serve_forever() and shutdown(), then server_close()."""
    store = MatchStore(match_store, match_versions, match_store_max_bytes) if match_store is not None else None
    handler = type("Handler", (StaticHandler,), {
        "root": root.resolve(),
        "cross_origin_isolation": cross_origin_isolation,
        "quiet": quiet,
        "basic_credentials": f"{basic_auth[0]}:{basic_auth[1]}".encode("utf-8") if basic_auth else None,
        "match_store": store,
        "rate_limiter": RateLimiter(MATCH_RATE_LIMIT, MATCH_RATE_WINDOW_SECONDS, clock) if store is not None else None,
        "forwarded_for": match_forwarded_for,
    })
    server = MatchServer((host, port), handler)
    if store is not None:
        store.sweep(clock())
        server.start_sweeper(store, clock)
    return server


def env_flag(environ, name: str) -> bool | None:
    """True or False for a variable that says 1/true/yes/on or 0/false/no/off, None for one that is unset, empty or says anything else."""
    value = environ.get(name, "").strip().lower()
    if value in ("1", "true", "yes", "on"):
        return True
    if value in ("0", "false", "no", "off"):
        return False
    return None


def basic_auth_from(environ, error) -> tuple[str, str] | None:
    """(user, password) from BASIC_AUTH_USER and BASIC_AUTH_PASSWORD, None when neither is set. `error(message)` is called (and does not
    return) for one without the other, an empty one, or a user with a colon, which a browser cannot send."""
    user = environ.get(BASIC_AUTH_USER_VARIABLE, "")
    password = environ.get(BASIC_AUTH_PASSWORD_VARIABLE, "")
    if not user and not password:
        return None
    if not user or not password:
        error(f"{BASIC_AUTH_USER_VARIABLE} and {BASIC_AUTH_PASSWORD_VARIABLE} must be set together and not empty")
    if ":" in user:
        error(f"{BASIC_AUTH_USER_VARIABLE} must not contain a colon")
    return user, password


def match_versions_from(text: str, error) -> tuple[int, int] | None:
    """(lowest, highest) from `N` or `N-M` (the value of MATCH_LOG_VERSIONS), None when empty. `error(message)` is called for anything else."""
    text = text.strip()
    if not text:
        return None
    found = MATCH_VERSIONS_PATTERN.fullmatch(text)
    low = int(found[1]) if found else 0
    high = int(found[2]) if found and found[2] is not None else low
    if not found or low > high:
        error(f"{MATCH_LOG_VERSIONS_VARIABLE} must be N or N-M with N <= M, not {text!r}")
    return low, high


def manifest_threads(root: Path) -> bool | None:
    """The `threads` member of manifest.json in `root`: True or False, None when there is no manifest, it cannot be read or it has no boolean `threads`."""
    try:
        manifest = json.loads((root / MANIFEST_NAME).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    threads = manifest.get("threads") if isinstance(manifest, dict) else None
    return threads if isinstance(threads, bool) else None


def decide_cross_origin_isolation(explicit: bool | None, source: str | None, root: Path) -> tuple[bool, str]:
    """Whether to send the two headers, and why. `explicit` is the answer of a switch or of COOP_COEP (None when neither was given) and
    `source` names it; then the manifest of `root`; with neither, on."""
    if explicit is not None:
        return explicit, f"{source} says so"
    threads = manifest_threads(root)
    if threads is not None:
        return threads, f"{MANIFEST_NAME} says threads: {'true' if threads else 'false'}, a {'threaded' if threads else 'single-thread'} build"
    return True, f"no {MANIFEST_NAME} in {root} (or it is not valid JSON, or has no boolean threads): on by default, the web build is threaded"


def parse_args(argv: list[str] | None = None, environ=None) -> argparse.Namespace:
    """The arguments. `cross_origin_isolation` is True, False or None (not asked: decide_cross_origin_isolation() reads the manifest);
    `isolation_source` names the switch or the variable that asked."""
    environ = os.environ if environ is None else environ
    parser = argparse.ArgumentParser(description="Serve the folder made by package.py.")
    parser.add_argument("--root", type=Path, default=Path("dist"), help="folder to serve (default: dist)")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"address to listen on (default: {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=int(environ.get("PORT") or DEFAULT_PORT), help="default: $PORT, else 8080")
    isolation = parser.add_mutually_exclusive_group()
    isolation.add_argument("--cross-origin-isolation", dest="cross_origin_isolation", action="store_const", const=True, default=None,
                           help="send Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy (also COOP_COEP=1); "
                                "without a switch or the variable the `threads` member of manifest.json decides, and without one the headers are sent")
    isolation.add_argument("--no-cross-origin-isolation", dest="cross_origin_isolation", action="store_const", const=False,
                           help="do not send them (also COOP_COEP=0): a threaded page then shows its message instead of starting")
    parser.add_argument("--quiet", action="store_true", help="no access log")
    args = parser.parse_args(argv)
    args.basic_auth = basic_auth_from(environ, parser.error)
    store = environ.get(MATCH_STORE_VARIABLE, "").strip()
    args.match_store = Path(store) if store else None
    megabytes = environ.get(MATCH_STORE_MAX_MB_VARIABLE, "").strip() or str(MATCH_STORE_MAX_MB_DEFAULT)
    if not re.fullmatch(r"[0-9]{1,9}", megabytes) or int(megabytes) < 1:
        parser.error(f"{MATCH_STORE_MAX_MB_VARIABLE} must be a whole number of megabytes, 1 or more, not {megabytes!r}")
    args.match_store_max_bytes = int(megabytes) << 20
    args.match_forwarded_for = bool(env_flag(environ, MATCH_FORWARDED_FOR_VARIABLE))
    args.match_versions = match_versions_from(environ.get(MATCH_LOG_VERSIONS_VARIABLE, ""), parser.error)
    args.isolation_source = None
    if args.cross_origin_isolation is not None:
        args.isolation_source = "--cross-origin-isolation" if args.cross_origin_isolation else "--no-cross-origin-isolation"
    else:
        args.cross_origin_isolation = env_flag(environ, COOP_COEP_VARIABLE)
        if args.cross_origin_isolation is not None:
            args.isolation_source = f"{COOP_COEP_VARIABLE}={'1' if args.cross_origin_isolation else '0'}"
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.root.is_dir():
        print(f"serve.py: {args.root} is not a folder", file=sys.stderr)
        return 1
    isolation, reason = decide_cross_origin_isolation(args.cross_origin_isolation, args.isolation_source, args.root)
    try:
        server = make_server(args.root, args.host, args.port, isolation, args.quiet, args.basic_auth, args.match_store,
                             args.match_versions, args.match_store_max_bytes, args.match_forwarded_for)
    except MatchStoreError as error:
        print(f"serve.py: {error}", file=sys.stderr)
        return 1
    # The password is never printed
    access = "password required" if args.basic_auth else "open"
    store = "off"
    if args.match_store is not None:
        store = str(args.match_store)
        if args.match_versions is None:
            store += f" (WARNING: {MATCH_LOG_VERSIONS_VARIABLE} is not set, so every first piece is refused)"
        if not args.basic_auth:
            store += " (WARNING: no Basic auth, so anybody can post, list, read and delete match logs)"
    print(f"serve.py: {args.root} on http://{args.host}:{server.server_address[1]} "
          f"(cross-origin isolation {'on' if isolation else 'off'}: {reason}; access: {access}; match store: {store})", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
