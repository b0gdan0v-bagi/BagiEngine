"""Tests of tools/web/serve.py: a real server on port 0 of 127.0.0.1 in a thread, asked with http.client.
Run: python -m unittest discover tools/web/tests"""

import ast
import contextlib
import gzip
import http.client
import io
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path

from web_fixtures import CONTENT, GAME, FakeBrotli, write_build_output

import package
import serve


class Response:
    def __init__(self, response: http.client.HTTPResponse):
        self.status = response.status
        self.headers = {name.lower(): value for name, value in response.getheaders()}
        self.body = response.read()


class ServerTestCase(unittest.TestCase):
    """The server over a dist folder made by package.py (with the fake brotli: its .br body is the marker plus the content)."""

    brotli = FakeBrotli
    cross_origin_isolation = False

    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        root = Path(self._temp.name)
        self.dist = root / "dist"
        package.package(write_build_output(root / "build"), self.dist, brotli_module=self.brotli)
        (root / "secret.txt").write_text("outside the served folder", encoding="utf-8")
        self.server = serve.make_server(self.dist, "127.0.0.1", 0, self.cross_origin_isolation, quiet=True)
        self.port = self.server.server_address[1]
        thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)
        thread.start()
        self.addCleanup(self.stop_server, thread)

    def stop_server(self, thread):
        self.server.shutdown()
        self.server.server_close()
        thread.join(timeout=10)

    def request(self, path: str, method: str = "GET", headers: dict | None = None) -> Response:
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            connection.request(method, path, headers=headers or {})
            return Response(connection.getresponse())
        finally:
            connection.close()


class ContentTypeTest(ServerTestCase):
    def test_each_file_has_its_content_type(self):
        for path, expected in (("/", "text/html; charset=utf-8"), ("/index.html", "text/html; charset=utf-8"),
                               (f"/{GAME}.js", "text/javascript"), (f"/{GAME}.wasm", "application/wasm"),
                               (f"/{GAME}.data", "application/octet-stream"), ("/manifest.json", "application/json")):
            with self.subTest(path):
                response = self.request(path)
                self.assertEqual(response.status, 200)
                self.assertEqual(response.headers["content-type"], expected)

    def test_the_content_type_is_that_of_the_file_also_when_it_is_sent_compressed(self):
        response = self.request(f"/{GAME}.wasm", headers={"Accept-Encoding": "gzip"})
        self.assertEqual(response.headers["content-type"], "application/wasm")

    def test_a_file_of_an_unknown_type_is_octet_stream(self):
        (self.dist / "thing.xyz").write_bytes(b"1")
        self.assertEqual(self.request("/thing.xyz").headers["content-type"], "application/octet-stream")


class EncodingTest(ServerTestCase):
    def test_without_accept_encoding_the_plain_file_is_sent(self):
        response = self.request(f"/{GAME}.wasm")
        self.assertEqual(response.body, CONTENT[f"{GAME}.wasm"])
        self.assertNotIn("content-encoding", response.headers)
        self.assertEqual(response.headers["vary"], "Accept-Encoding")
        self.assertEqual(int(response.headers["content-length"]), len(response.body))

    def test_brotli_is_preferred_over_gzip(self):
        response = self.request(f"/{GAME}.wasm", headers={"Accept-Encoding": "gzip, deflate, br"})
        self.assertEqual(response.headers["content-encoding"], "br")
        self.assertEqual(response.body, FakeBrotli.MARKER + CONTENT[f"{GAME}.wasm"])
        self.assertEqual(response.headers["vary"], "Accept-Encoding")
        self.assertEqual(int(response.headers["content-length"]), len(response.body))

    def test_gzip_when_only_gzip_is_accepted(self):
        response = self.request(f"/{GAME}.js", headers={"Accept-Encoding": "gzip"})
        self.assertEqual(response.headers["content-encoding"], "gzip")
        self.assertEqual(gzip.decompress(response.body), CONTENT[f"{GAME}.js"])
        self.assertEqual(int(response.headers["content-length"]), len(response.body))

    def test_an_encoding_with_q_0_is_not_used(self):
        response = self.request(f"/{GAME}.js", headers={"Accept-Encoding": "br;q=0, gzip;q=0.5"})
        self.assertEqual(response.headers["content-encoding"], "gzip")
        response = self.request(f"/{GAME}.js", headers={"Accept-Encoding": "br;q=0, gzip;q=0"})
        self.assertNotIn("content-encoding", response.headers)

    def test_a_star_accepts_the_best_variant(self):
        self.assertEqual(self.request(f"/{GAME}.js", headers={"Accept-Encoding": "*"}).headers["content-encoding"], "br")

    def test_an_encoding_the_client_does_not_know_gets_the_plain_file(self):
        response = self.request(f"/{GAME}.js", headers={"Accept-Encoding": "deflate, zstd"})
        self.assertNotIn("content-encoding", response.headers)
        self.assertEqual(response.body, CONTENT[f"{GAME}.js"])

    def test_a_file_without_variants_has_no_vary_and_no_encoding(self):
        (self.dist / "plain.txt").write_bytes(b"plain")
        response = self.request("/plain.txt", headers={"Accept-Encoding": "br, gzip"})
        self.assertNotIn("content-encoding", response.headers)
        self.assertNotIn("vary", response.headers)

    def test_the_variants_are_not_files_of_their_own(self):
        for path in (f"/{GAME}.wasm.gz", f"/{GAME}.wasm.br"):
            with self.subTest(path):
                self.assertEqual(self.request(path).status, 404)

    def test_the_head_of_a_compressed_answer_has_the_headers_and_no_body(self):
        get = self.request(f"/{GAME}.wasm", headers={"Accept-Encoding": "br"})
        head = self.request(f"/{GAME}.wasm", method="HEAD", headers={"Accept-Encoding": "br"})
        self.assertEqual(head.body, b"")
        for name in ("content-length", "content-encoding", "content-type", "etag", "vary", "cache-control"):
            self.assertEqual(head.headers[name], get.headers[name], name)


class WithoutBrotliTest(ServerTestCase):
    brotli = None

    def test_br_is_not_offered_when_the_package_has_no_br_copy(self):
        response = self.request(f"/{GAME}.js", headers={"Accept-Encoding": "br, gzip"})
        self.assertEqual(response.headers["content-encoding"], "gzip")


class CachingTest(ServerTestCase):
    def test_the_page_and_the_manifest_are_no_cache(self):
        for path in ("/", "/index.html", "/manifest.json"):
            with self.subTest(path):
                self.assertEqual(self.request(path).headers["cache-control"], "no-cache")

    def test_the_other_files_are_cached_for_an_hour_and_never_immutable(self):
        for path in (f"/{GAME}.js", f"/{GAME}.wasm", f"/{GAME}.data"):
            with self.subTest(path):
                cache = self.request(path).headers["cache-control"]
                self.assertEqual(cache, "public, max-age=3600")
                self.assertNotIn("immutable", cache)

    def test_a_file_named_like_the_page_in_a_subfolder_is_cached_like_any_file(self):
        (self.dist / "sub").mkdir()
        (self.dist / "sub" / "index.html").write_bytes(b"x")
        self.assertEqual(self.request("/sub/index.html").headers["cache-control"], "public, max-age=3600")

    def test_an_unchanged_file_answers_304_without_a_body(self):
        for path in ("/index.html", f"/{GAME}.wasm"):
            with self.subTest(path):
                first = self.request(path)
                etag = first.headers["etag"]
                second = self.request(path, headers={"If-None-Match": etag})
                self.assertEqual(second.status, 304)
                self.assertEqual(second.body, b"")
                self.assertEqual(second.headers["etag"], etag)
                self.assertEqual(second.headers["cache-control"], first.headers["cache-control"])

    def test_if_none_match_takes_a_list_a_weak_tag_and_a_star(self):
        etag = self.request(f"/{GAME}.js").headers["etag"]
        for header in (f'"nope", {etag}', f"W/{etag}", "*"):
            with self.subTest(header):
                self.assertEqual(self.request(f"/{GAME}.js", headers={"If-None-Match": header}).status, 304)
        self.assertEqual(self.request(f"/{GAME}.js", headers={"If-None-Match": '"nope"'}).status, 200)

    def test_each_encoding_has_its_own_tag(self):
        tags = {self.request(f"/{GAME}.js", headers={"Accept-Encoding": coding}).headers["etag"] for coding in ("identity", "gzip", "br")}
        self.assertEqual(len(tags), 3)
        plain = self.request(f"/{GAME}.js").headers["etag"]
        self.assertEqual(self.request(f"/{GAME}.js", headers={"If-None-Match": plain, "Accept-Encoding": "gzip"}).status, 200)

    def test_a_changed_file_has_a_new_tag(self):
        before = self.request("/manifest.json").headers["etag"]
        (self.dist / "manifest.json").write_text("{}  \n", encoding="utf-8")
        self.assertNotEqual(self.request("/manifest.json").headers["etag"], before)
        self.assertEqual(self.request("/manifest.json", headers={"If-None-Match": before}).status, 200)


class RoutingTest(ServerTestCase):
    def test_healthz_is_200_ok(self):
        response = self.request("/healthz")
        self.assertEqual((response.status, response.body), (200, b"ok"))
        self.assertEqual(self.request("/healthz", method="HEAD").status, 200)

    def test_a_query_string_is_ignored(self):
        self.assertEqual(self.request(f"/{GAME}.js?v=3").status, 200)
        self.assertEqual(self.request("/healthz?probe=1").status, 200)

    def test_a_missing_file_is_404(self):
        self.assertEqual(self.request("/nothing.js").status, 404)
        self.assertEqual(self.request("/nothing.js", method="HEAD").status, 404)

    def test_a_folder_is_404_except_the_root(self):
        (self.dist / "sub").mkdir()
        self.assertEqual(self.request("/sub").status, 404)
        self.assertEqual(self.request("/sub/").status, 404)
        self.assertEqual(self.request("/").status, 200)

    def test_a_path_that_leaves_the_folder_is_404(self):
        for path in ("/../secret.txt", "/%2e%2e/secret.txt", "/..%2fsecret.txt", "/a/../../secret.txt", "/%2E%2E%2Fsecret.txt",
                     "/..\\secret.txt", "/..%5csecret.txt", "/sub/../../secret.txt", "//../secret.txt"):
            with self.subTest(path):
                response = self.request(path)
                self.assertEqual(response.status, 404)
                self.assertNotIn(b"outside the served folder", response.body)

    def test_a_nul_byte_in_the_path_is_404(self):
        self.assertEqual(self.request("/index.html%00.js").status, 404)

    def test_a_link_that_points_out_of_the_folder_is_404(self):
        link = self.dist / "link.txt"
        try:
            link.symlink_to(self.dist.parent / "secret.txt")
        except (OSError, NotImplementedError):
            self.skipTest("no symbolic links here")
        self.assertEqual(self.request("/link.txt").status, 404)

    def test_other_methods_are_405(self):
        for method in ("POST", "PUT", "DELETE", "PATCH", "OPTIONS", "TRACE", "FOO"):
            with self.subTest(method):
                response = self.request("/index.html", method=method)
                self.assertEqual(response.status, 405)
                self.assertEqual(response.headers["allow"], "GET, HEAD")

    def test_the_server_header_does_not_name_the_python_version(self):
        server = self.request("/healthz").headers["server"]
        self.assertIn("BagiWebServe", server)
        self.assertNotIn("Python", server)
        self.assertEqual(server, "BagiWebServe")

    def test_a_405_closes_the_connection_so_an_unread_body_is_not_a_request(self):
        body = b"GET /healthz HTTP/1.1\r\n\r\n"
        head = b"POST /index.html HTTP/1.1\r\nHost: x\r\nContent-Length: " + str(len(body)).encode() + b"\r\n\r\n"
        with socket.create_connection(("127.0.0.1", self.port), timeout=10) as connection:
            connection.sendall(head + body)
            received = b""
            while True:  # to the end of the stream: a timeout here means the server kept the connection open
                chunk = connection.recv(65536)
                if not chunk:
                    break
                received += chunk
        self.assertTrue(received.startswith(b"HTTP/1.1 405"), received)
        self.assertEqual(received.count(b"HTTP/1."), 1, received)  # the body was not taken for a second request

    def test_several_requests_on_one_connection_work(self):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            for path in (f"/{GAME}.wasm", "/nothing", "/healthz", f"/{GAME}.js"):
                connection.request("GET", path)
                response = connection.getresponse()
                response.read()
            self.assertEqual(response.status, 200)
        finally:
            connection.close()


class CrossOriginIsolationTest(ServerTestCase):
    def test_off_by_default(self):
        for path in ("/", "/healthz", "/nothing"):
            with self.subTest(path):
                headers = self.request(path).headers
                self.assertNotIn("cross-origin-opener-policy", headers)
                self.assertNotIn("cross-origin-embedder-policy", headers)


class CrossOriginIsolationOnTest(ServerTestCase):
    cross_origin_isolation = True

    def test_every_answer_carries_the_two_headers(self):
        for path, method in (("/", "GET"), (f"/{GAME}.wasm", "GET"), ("/index.html", "HEAD"), ("/healthz", "GET"), ("/nothing", "GET"),
                             ("/index.html", "POST")):
            with self.subTest(path=path, method=method):
                headers = self.request(path, method=method).headers
                self.assertEqual(headers["cross-origin-opener-policy"], "same-origin")
                self.assertEqual(headers["cross-origin-embedder-policy"], "require-corp")

    def test_a_304_carries_them_too(self):
        etag = self.request("/index.html").headers["etag"]
        response = self.request("/index.html", headers={"If-None-Match": etag})
        self.assertEqual(response.status, 304)
        self.assertEqual(response.headers["cross-origin-embedder-policy"], "require-corp")


class IsolationDecisionTest(unittest.TestCase):
    """Who decides on the two headers: a switch, then COOP_COEP, then manifest.json's `threads`, then on."""

    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.root = Path(self._temp.name)

    def write_manifest(self, text: str):
        (self.root / "manifest.json").write_text(text, encoding="utf-8")

    def decide(self, argv=(), environ=None):
        args = serve.parse_args(list(argv), environ=environ or {})
        return serve.decide_cross_origin_isolation(args.cross_origin_isolation, args.isolation_source, self.root)

    def test_without_a_manifest_the_headers_are_on(self):
        on, reason = self.decide()
        self.assertTrue(on)
        self.assertIn("by default", reason)

    def test_the_manifest_decides_when_nothing_was_asked(self):
        self.write_manifest('{"game": "G", "threads": true, "files": {}}')
        on, reason = self.decide()
        self.assertTrue(on)
        self.assertIn("threads: true", reason)
        self.write_manifest('{"game": "G", "threads": false, "files": {}}')
        on, reason = self.decide()
        self.assertFalse(on)
        self.assertIn("threads: false", reason)

    def test_a_manifest_without_a_usable_threads_member_counts_as_no_manifest(self):
        for text in ("{}", '{"threads": "no"}', '{"threads": 0}', "[]", "not json", ""):
            with self.subTest(text=text):
                self.write_manifest(text)
                self.assertTrue(self.decide()[0])

    def test_the_switches_win_over_the_manifest(self):
        self.write_manifest('{"threads": false}')
        on, reason = self.decide(["--cross-origin-isolation"])
        self.assertTrue(on)
        self.assertIn("--cross-origin-isolation", reason)
        self.write_manifest('{"threads": true}')
        on, reason = self.decide(["--no-cross-origin-isolation"])
        self.assertFalse(on)
        self.assertIn("--no-cross-origin-isolation", reason)

    def test_the_variable_wins_over_the_manifest_and_the_switch_over_the_variable(self):
        self.write_manifest('{"threads": false}')
        on, reason = self.decide(environ={"COOP_COEP": "1"})
        self.assertTrue(on)
        self.assertIn("COOP_COEP=1", reason)
        self.write_manifest('{"threads": true}')
        on, reason = self.decide(environ={"COOP_COEP": "0"})
        self.assertFalse(on)
        self.assertIn("COOP_COEP=0", reason)
        self.assertTrue(self.decide(["--cross-origin-isolation"], environ={"COOP_COEP": "0"})[0])
        self.assertFalse(self.decide(["--no-cross-origin-isolation"], environ={"COOP_COEP": "1"})[0])

    def test_an_empty_or_unknown_variable_asks_nothing(self):
        self.write_manifest('{"threads": false}')
        for value in ("", "maybe", "  "):
            with self.subTest(value=value):
                self.assertFalse(self.decide(environ={"COOP_COEP": value})[0])

    def test_the_two_switches_exclude_each_other(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            serve.parse_args(["--cross-origin-isolation", "--no-cross-origin-isolation"], environ={})


class ServedHeadersFollowTheManifestTest(unittest.TestCase):
    """serve.py as a program, the way the image starts it (the CMD of the Dockerfile, no switch) and a person at the keyboard does."""

    def start(self, cwd: Path, argv: list[str], environment: dict):
        process = subprocess.Popen([sys.executable, str(cwd / "serve.py"), *argv], cwd=cwd, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, env=environment, text=True)
        watchdog = threading.Timer(30, process.kill)
        watchdog.start()

        def stop():
            watchdog.cancel()
            process.kill()
            process.wait(timeout=10)
            process.stdout.close()
        self.addCleanup(stop)
        line = process.stdout.readline()
        match = re.search(r"http://[\d.]+:(\d+) ", line)
        self.assertIsNotNone(match, line)
        return int(match.group(1)), line

    def make_service(self, threads: bool | None) -> Path:
        """What the Dockerfile copies: serve.py and the dist/ that package.py makes (`threads` None: a dist/ without a manifest)."""
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        service = Path(temp.name)
        shutil.copyfile(Path(serve.__file__), service / "serve.py")
        package.package(write_build_output(service / "build"), service / "dist", brotli_module=None, threads=threads is not False)
        if threads is None:
            (service / "dist" / "manifest.json").unlink()
        return service

    def request(self, port: int, path: str, headers: dict | None = None) -> Response:
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        try:
            connection.request("GET", path, headers=headers or {})
            return Response(connection.getresponse())
        finally:
            connection.close()

    def environment(self, **extra) -> dict:
        environment = {name: value for name, value in os.environ.items() if name != "COOP_COEP"}
        environment["PORT"] = "0"
        environment.update(extra)
        return environment

    def docker_arguments(self) -> list[str]:
        """The arguments of the CMD of tools/web/Dockerfile after `python serve.py`."""
        text = (Path(serve.__file__).parent / "Dockerfile").read_text(encoding="utf-8")
        command = json.loads(re.search(r"^CMD (\[.*\])$", text, flags=re.MULTILINE).group(1))
        self.assertEqual(command[:2], ["python", "serve.py"])
        return command[2:]

    def test_the_image_command_sends_both_headers_for_a_threaded_dist_on_every_kind_of_answer(self):
        service = self.make_service(threads=True)
        port, line = self.start(service, self.docker_arguments(), self.environment())
        self.assertIn("cross-origin isolation on", line)
        self.assertIn("threads: true", line)
        etag = self.request(port, "/").headers["etag"]
        for path, headers, status in (("/", None, 200), (f"/{GAME}.wasm", None, 200), ("/nothing.js", None, 404),
                                      ("/", {"If-None-Match": etag}, 304), ("/healthz", None, 200)):
            with self.subTest(path=path, status=status):
                response = self.request(port, path, headers)
                self.assertEqual(response.status, status)
                self.assertEqual(response.headers["cross-origin-opener-policy"], "same-origin")
                self.assertEqual(response.headers["cross-origin-embedder-policy"], "require-corp")

    def test_the_image_command_sends_them_for_a_dist_without_a_manifest_too(self):
        service = self.make_service(threads=None)
        port, line = self.start(service, self.docker_arguments(), self.environment())
        self.assertIn("cross-origin isolation on", line)
        self.assertEqual(self.request(port, "/").headers["cross-origin-embedder-policy"], "require-corp")

    def test_the_image_command_sends_none_for_a_single_thread_dist(self):
        service = self.make_service(threads=False)
        port, line = self.start(service, self.docker_arguments(), self.environment())
        self.assertIn("cross-origin isolation off", line)
        self.assertIn("threads: false", line)
        for path in ("/", f"/{GAME}.wasm", "/nothing.js"):
            with self.subTest(path=path):
                headers = self.request(port, path).headers
                self.assertNotIn("cross-origin-opener-policy", headers)
                self.assertNotIn("cross-origin-embedder-policy", headers)

    def test_a_switch_turns_the_headers_off_for_a_threaded_dist(self):
        service = self.make_service(threads=True)
        port, line = self.start(service, [*self.docker_arguments(), "--no-cross-origin-isolation"], self.environment())
        self.assertIn("cross-origin isolation off", line)
        self.assertNotIn("cross-origin-embedder-policy", self.request(port, "/").headers)

    def test_the_variable_turns_them_off_too(self):
        service = self.make_service(threads=True)
        port, line = self.start(service, self.docker_arguments(), self.environment(COOP_COEP="0"))
        self.assertIn("COOP_COEP=0", line)
        self.assertNotIn("cross-origin-opener-policy", self.request(port, "/").headers)


class ArgumentsTest(unittest.TestCase):
    def test_the_port_comes_from_the_environment_and_defaults_to_8080(self):
        self.assertEqual(serve.parse_args([], environ={}).port, 8080)
        self.assertEqual(serve.parse_args([], environ={"PORT": ""}).port, 8080)
        self.assertEqual(serve.parse_args([], environ={"PORT": "4321"}).port, 4321)
        self.assertEqual(serve.parse_args(["--port", "99"], environ={"PORT": "4321"}).port, 99)

    def test_cross_origin_isolation_is_asked_by_a_switch_or_by_coop_coep_and_is_none_otherwise(self):
        self.assertIsNone(serve.parse_args([], environ={}).cross_origin_isolation)
        self.assertIs(serve.parse_args(["--cross-origin-isolation"], environ={}).cross_origin_isolation, True)
        self.assertIs(serve.parse_args(["--no-cross-origin-isolation"], environ={}).cross_origin_isolation, False)
        self.assertIs(serve.parse_args([], environ={"COOP_COEP": "1"}).cross_origin_isolation, True)
        self.assertIs(serve.parse_args([], environ={"COOP_COEP": "0"}).cross_origin_isolation, False)
        self.assertIsNone(serve.parse_args([], environ={"COOP_COEP": ""}).cross_origin_isolation)

    def test_the_root_defaults_to_dist_and_the_host_to_all_interfaces(self):
        args = serve.parse_args([], environ={})
        self.assertEqual(args.root, Path("dist"))
        self.assertEqual(args.host, "0.0.0.0")

    def test_accept_encoding_parsing(self):
        self.assertEqual(serve.accepted_encodings(None), set())
        self.assertEqual(serve.accepted_encodings("gzip, br;q=0.8"), {"gzip", "br"})
        self.assertEqual(serve.accepted_encodings("GZIP"), {"gzip"})
        self.assertEqual(serve.accepted_encodings("*, br;q=0"), {"gzip"})
        self.assertEqual(serve.accepted_encodings("gzip;q=junk"), set())


class StandaloneTest(unittest.TestCase):
    """The file is the whole image of the Docker service: it imports nothing of this repository, and runs as a program."""

    def test_serve_py_imports_only_the_standard_library(self):
        tree = ast.parse(Path(serve.__file__).read_text(encoding="utf-8"))
        modules = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                modules.update(alias.name.split(".")[0] for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.level == 0:
                modules.add(node.module.split(".")[0])
        self.assertEqual(modules - set(sys.stdlib_module_names), set())

    def test_the_program_listens_on_the_port_of_the_environment_and_answers_healthz(self):
        with tempfile.TemporaryDirectory() as temp:
            dist = Path(temp) / "dist"
            dist.mkdir()
            (dist / "index.html").write_bytes(b"<html></html>")
            environment = dict(os.environ, PORT="0", COOP_COEP="1")
            process = subprocess.Popen([sys.executable, serve.__file__, "--root", str(dist), "--host", "127.0.0.1", "--quiet"],
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=environment, text=True)
            watchdog = threading.Timer(30, process.kill)
            watchdog.start()
            try:
                line = process.stdout.readline()
                self.assertIn("cross-origin isolation on", line)
                port = int(line.split("http://127.0.0.1:")[1].split(" ")[0])
                connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
                connection.request("GET", "/healthz")
                response = connection.getresponse()
                self.assertEqual((response.status, response.read()), (200, b"ok"))
                self.assertEqual(response.getheader("Cross-Origin-Embedder-Policy"), "require-corp")
                connection.close()
            finally:
                watchdog.cancel()
                process.kill()
                process.wait(timeout=10)
                process.stdout.close()


if __name__ == "__main__":
    unittest.main()
