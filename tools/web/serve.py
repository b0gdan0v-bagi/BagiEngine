"""The static server of the web build, the process the Railway service runs. Standard library only, and this file imports none of the
others of tools/web: the Docker image copies only it.

    python serve.py --root dist                      # PORT from the environment, 8080 without it
    python serve.py --root dist --port 9000 --cross-origin-isolation

It serves the folder made by package.py. For a file `x` it answers with `x.br` or `x.gz` when one exists and the request accepts that
encoding (Content-Encoding, Vary: Accept-Encoding, the Content-Length of the encoded body). The names are not hashed (BagiMatch3.wasm
is the same name in every build), so nothing is `immutable`: index.html and manifest.json are `no-cache` (the browser asks every time,
and an ETag makes the answer a 304), everything else may be reused for an hour and is revalidated with its ETag after that.
GET /healthz answers 200 `ok` for the health check of the platform.

--cross-origin-isolation (or the environment variable COOP_COEP=1) adds Cross-Origin-Opener-Policy: same-origin and
Cross-Origin-Embedder-Policy: require-corp to every answer. Off by default: the single-thread build needs no SharedArrayBuffer, and
the headers break pages that embed other origins.
"""

import argparse
import os
import posixpath
import shutil
import sys
from email.utils import formatdate
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

DEFAULT_PORT = 8080
# Railway reaches the service from outside the container
DEFAULT_HOST = "0.0.0.0"
INDEX_NAME = "index.html"
HEALTH_PATH = "/healthz"
NO_CACHE_FILES = (INDEX_NAME, "manifest.json")
NO_CACHE = "no-cache"
SHORT_CACHE = "public, max-age=3600"
COPY_CHUNK = 1 << 20
# Encodings in the order the server prefers them: brotli is smaller than gzip
ENCODINGS = (("br", ".br"), ("gzip", ".gz"))
VARIANT_SUFFIXES = tuple(suffix for _, suffix in ENCODINGS)

# Named here, not left to mimetypes: on Windows it reads the registry and has answered text/plain for .js
CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript",
    ".mjs": "text/javascript",
    ".wasm": "application/wasm",
    ".data": "application/octet-stream",
    ".json": "application/json",
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


class StaticHandler(BaseHTTPRequestHandler):
    """GET and HEAD of the files below `root`; `root`, `cross_origin_isolation` and `quiet` are set by make_server()."""

    root: Path
    cross_origin_isolation = False
    quiet = False
    protocol_version = "HTTP/1.1"
    server_version = "BagiWebServe"
    # The Server header names the program and not the Python version
    sys_version = ""

    def version_string(self):
        return self.server_version

    def log_message(self, format, *args):
        if not self.quiet:
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

    def do_unsupported(self):
        # The body of a POST is not read, so on a kept connection it would be parsed as the next request
        self.send_response(HTTPStatus.METHOD_NOT_ALLOWED)
        self.send_header("Allow", "GET, HEAD")
        # send_header("Connection", "close") also sets close_connection
        self.send_header("Connection", "close")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def __getattr__(self, name):
        # Every method other than GET and HEAD (TRACE and CONNECT included, and a made-up one) is 405, not the 501 page of the base class
        if name.startswith("do_"):
            return self.do_unsupported
        raise AttributeError(name)

    def serve(self, send_body: bool):
        url_path = urlsplit(self.path).path
        if url_path == HEALTH_PATH:
            self.send_text(HTTPStatus.OK, "ok", send_body)
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
            cache = NO_CACHE if path.name in NO_CACHE_FILES and path.parent == self.root else SHORT_CACHE
            headers = [("ETag", etag), ("Cache-Control", cache), ("Last-Modified", formatdate(info.st_mtime, usegmt=True))]
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

    def send_text(self, status: HTTPStatus, text: str, send_body: bool):
        body = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if send_body:
            self.wfile.write(body)


def make_server(root: Path, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT, cross_origin_isolation: bool = False,
                quiet: bool = False) -> ThreadingHTTPServer:
    """A server for `root`; port 0 takes a free one (server.server_address[1]). The caller runs serve_forever() and shutdown()."""
    handler = type("Handler", (StaticHandler,), {
        "root": root.resolve(),
        "cross_origin_isolation": cross_origin_isolation,
        "quiet": quiet,
    })
    server = ThreadingHTTPServer((host, port), handler)
    server.daemon_threads = True
    return server


def env_flag(environ, name: str) -> bool:
    return environ.get(name, "").strip().lower() in ("1", "true", "yes", "on")


def parse_args(argv: list[str] | None = None, environ=None) -> argparse.Namespace:
    environ = os.environ if environ is None else environ
    parser = argparse.ArgumentParser(description="Serve the folder made by package.py.")
    parser.add_argument("--root", type=Path, default=Path("dist"), help="folder to serve (default: dist)")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"address to listen on (default: {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=int(environ.get("PORT") or DEFAULT_PORT), help="default: $PORT, else 8080")
    parser.add_argument("--cross-origin-isolation", action="store_true", default=env_flag(environ, "COOP_COEP"),
                        help="send Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy (also COOP_COEP=1)")
    parser.add_argument("--quiet", action="store_true", help="no access log")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.root.is_dir():
        print(f"serve.py: {args.root} is not a folder", file=sys.stderr)
        return 1
    server = make_server(args.root, args.host, args.port, args.cross_origin_isolation, args.quiet)
    isolation = "on" if args.cross_origin_isolation else "off"
    print(f"serve.py: {args.root} on http://{args.host}:{server.server_address[1]} (cross-origin isolation {isolation})", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
