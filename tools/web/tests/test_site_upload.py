"""Tests of tools/web/site_upload.py: the tar, the sha256, the token and the PUT, against a server on port 0 of 127.0.0.1 in a thread.

No network and no real site. Every exception a test provokes goes through `refused()`, and `tearDown` looks for the marker of the test
token in the text and the `repr` of each of them and in everything the module printed."""

import contextlib
import hashlib
import http.client
import http.server
import io
import json
import os
import socket
import tarfile
import tempfile
import threading
import unittest
from pathlib import Path

import web_fixtures  # noqa: F401  (puts tools/web on sys.path)
import site_upload

SITE = "engine.example.dev"
PAGE = "demo"
MARKER = "TOKENMARKER7f3a"
TOKEN = f"{MARKER}-abc123"
SHA = hashlib.sha256(b"x").hexdigest()


class SiteHandler(http.server.BaseHTTPRequestHandler):
    """Records the request and answers as `server.script` says: `mode` is `read` (read the body, then answer), `early` (answer at once and
    close with the body unread), `drop` (close without an answer) or `hang` (read the body and never answer until `server.release` is set)."""

    def do_PUT(self) -> None:
        server = self.server
        length = int(self.headers.get("Content-Length", "0"))
        record = {"method": self.command, "path": self.path, "headers": {key.lower(): value for key, value in self.headers.items()}}
        server.requests.append(record)
        if server.mode == "read":
            record["body"] = self.rfile.read(length)
        elif server.mode == "drop":
            return
        elif server.mode == "hang":
            record["body"] = self.rfile.read(length)
            server.release.wait(10)
            return
        body = server.answer
        self.send_response(server.status)
        for name, value in server.extra_headers.items():
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def log_message(self, *args) -> None:
        pass


class RecordingServer(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self) -> None:
        super().__init__(("127.0.0.1", 0), SiteHandler)
        self.requests: list[dict] = []
        self.mode = "read"
        self.status = 200
        self.answer = b"{}"
        self.extra_headers: dict[str, str] = {}
        self.release = threading.Event()

    @property
    def port(self) -> int:
        return self.server_address[1]

    def answers(self, status: int, payload=None, raw: bytes | None = None) -> None:
        self.status = status
        self.answer = raw if raw is not None else json.dumps(payload if payload is not None else {}).encode()


class SiteUploadTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = Path(tempfile.mkdtemp(prefix="site-upload-"))
        self.addCleanup(self.rmtree, self.temp)
        self.raised: list[BaseException] = []
        self.printed = io.StringIO()
        stack = contextlib.ExitStack()
        stack.enter_context(contextlib.redirect_stdout(self.printed))
        stack.enter_context(contextlib.redirect_stderr(self.printed))
        self.addCleanup(stack.close)

    @staticmethod
    def rmtree(path: Path) -> None:
        import shutil
        shutil.rmtree(path, ignore_errors=True)

    def tearDown(self) -> None:
        for error in self.raised:
            self.assertNotIn(MARKER, str(error))
            self.assertNotIn(MARKER, repr(error))
            self.assertNotIn(MARKER, repr(error.args))
        self.assertNotIn(MARKER, self.printed.getvalue())

    def refused(self, call, *args, **kwargs) -> site_upload.UploadError:
        with self.assertRaises(site_upload.UploadError) as caught:
            call(*args, **kwargs)
        self.raised.append(caught.exception)
        return caught.exception

    def start_server(self) -> RecordingServer:
        server = RecordingServer()
        thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)
        thread.start()

        def stop() -> None:
            server.release.set()
            server.shutdown()
            server.server_close()
            thread.join(5)

        self.addCleanup(stop)
        return server

    def connector(self, server: RecordingServer, hosts: list[str] | None = None):
        def connect(host: str, timeout: float) -> http.client.HTTPConnection:
            if hosts is not None:
                hosts.append(host)
            return http.client.HTTPConnection("127.0.0.1", server.port, timeout=timeout)
        return connect

    def make_package(self, name: str = "dist") -> Path:
        dist = self.temp / name
        (dist / "icons").mkdir(parents=True)
        (dist / "index.html").write_bytes(b"<html></html>\n")
        (dist / "manifest.json").write_bytes(b'{"game": "BagiFake", "files": {}}\n')
        (dist / "BagiFake.wasm").write_bytes(bytes(range(256)) * 40)
        (dist / "icons" / "icon-192.png").write_bytes(b"\x89PNG fake")
        return dist

    def tar_of(self, dist: Path, size_hint: int = 0) -> tuple[Path, str]:
        if size_hint:
            (dist / "big.bin").write_bytes(os.urandom(size_hint))
        tar_path = self.temp / "package.tar"
        site_upload.build_tar(dist, tar_path)
        return tar_path, site_upload.file_sha256(tar_path)


class BuildTarTest(SiteUploadTestCase):
    def test_files_sit_at_the_root_and_a_folder_keeps_its_name(self) -> None:
        dist = self.make_package()
        tar_path = self.temp / "package.tar"
        count = site_upload.build_tar(dist, tar_path)
        with tarfile.open(tar_path) as tar:
            members = {member.name: member for member in tar.getmembers()}
            self.assertEqual(sorted(members), ["BagiFake.wasm", "icons", "icons/icon-192.png", "index.html", "manifest.json"])
            self.assertTrue(members["icons"].isdir())
            self.assertEqual(tar.extractfile("index.html").read(), b"<html></html>\n")
            self.assertEqual(tar.extractfile("icons/icon-192.png").read(), b"\x89PNG fake")
        self.assertEqual(count, 4)

    def test_it_is_a_plain_uncompressed_tar_with_owner_zero(self) -> None:
        dist = self.make_package()
        tar_path = self.temp / "package.tar"
        site_upload.build_tar(dist, tar_path)
        self.assertFalse(tar_path.read_bytes()[:2] == b"\x1f\x8b")
        with tarfile.open(tar_path) as tar:
            for member in tar.getmembers():
                self.assertEqual((member.uid, member.gid, member.uname, member.gname), (0, 0, "", ""), member.name)

    def test_macos_attribute_files_are_left_out_at_every_depth(self) -> None:
        dist = self.make_package()
        (dist / "._index.html").write_bytes(b"x")
        (dist / ".DS_Store").write_bytes(b"x")
        (dist / "icons" / "._icon-192.png").write_bytes(b"x")
        (dist / "icons" / ".DS_Store").write_bytes(b"x")
        tar_path = self.temp / "package.tar"
        site_upload.build_tar(dist, tar_path)
        with tarfile.open(tar_path) as tar:
            self.assertEqual(sorted(tar.getnames()), ["BagiFake.wasm", "icons", "icons/icon-192.png", "index.html", "manifest.json"])

    @unittest.skipUnless(hasattr(os, "symlink") and os.name != "nt", "needs symbolic links")
    def test_a_symlink_is_refused_and_named(self) -> None:
        dist = self.make_package()
        (dist / "icons" / "link.png").symlink_to(dist / "index.html")
        error = self.refused(site_upload.build_tar, dist, self.temp / "package.tar")
        self.assertIn("link.png", str(error))

    @unittest.skipUnless(hasattr(os, "mkfifo"), "needs a named pipe")
    def test_another_kind_of_file_is_refused_and_named(self) -> None:
        dist = self.make_package()
        os.mkfifo(dist / "pipe")
        error = self.refused(site_upload.build_tar, dist, self.temp / "package.tar")
        self.assertIn("pipe", str(error))

    def test_a_missing_folder_is_refused(self) -> None:
        self.refused(site_upload.build_tar, self.temp / "nothing", self.temp / "package.tar")

    def test_sha256_is_the_digest_of_the_file(self) -> None:
        path = self.temp / "data.bin"
        path.write_bytes(os.urandom(3 * site_upload.PIECE_BYTES + 5))
        self.assertEqual(site_upload.file_sha256(path), hashlib.sha256(path.read_bytes()).hexdigest())


class UploadTest(SiteUploadTestCase):
    def upload(self, server: RecordingServer, tar_path: Path, sha: str, **kwargs):
        return site_upload.upload(SITE, PAGE, tar_path, sha, TOKEN, connect=self.connector(server, kwargs.pop("hosts", None)), **kwargs)

    def test_the_request_carries_the_contract_and_the_tar_unchanged(self) -> None:
        server = self.start_server()
        server.answers(200, {"page": PAGE, "game": "BagiFake", "bytes": 1})
        tar_path, sha = self.tar_of(self.make_package(), size_hint=3 * site_upload.PIECE_BYTES + 17)
        hosts: list[str] = []
        answer = self.upload(server, tar_path, sha, hosts=hosts)
        self.assertEqual(answer, {"page": PAGE, "game": "BagiFake", "bytes": 1})
        self.assertEqual(hosts, [SITE])
        (request,) = server.requests
        self.assertEqual((request["method"], request["path"]), ("PUT", f"/api/v1/builds/{PAGE}"))
        headers = request["headers"]
        self.assertEqual(headers["authorization"], f"Bearer {TOKEN}")
        self.assertEqual(headers["content-type"], "application/x-tar")
        self.assertEqual(headers["content-length"], str(tar_path.stat().st_size))
        self.assertEqual(headers["x-content-sha256"], hashlib.sha256(request["body"]).hexdigest())
        self.assertEqual(headers["x-content-sha256"], sha)
        self.assertNotIn("transfer-encoding", headers)
        self.assertEqual(request["body"], tar_path.read_bytes())

    def test_each_refusal_is_an_error_with_the_status_and_the_detail(self) -> None:
        tar_path, sha = self.tar_of(self.make_package())
        for status in (400, 401, 404, 408, 409, 411, 413, 422, 503, 507):
            with self.subTest(status=status):
                server = self.start_server()
                server.answers(status, {"detail": f"detail of {status}"})
                error = self.refused(self.upload, server, tar_path, sha)
                self.assertIn(str(status), str(error))
                self.assertIn(f"detail of {status}", str(error))

    def test_an_answer_without_json_gives_no_detail(self) -> None:
        tar_path, sha = self.tar_of(self.make_package())
        for raw in (b"<html>Bad gateway</html>", b"", b'["detail"]', b'{"detail": 5}', b'{"detail": "   "}'):
            with self.subTest(raw=raw):
                server = self.start_server()
                server.answers(502, raw=raw)
                error = self.refused(self.upload, server, tar_path, sha)
                self.assertIn("502", str(error))
                self.assertIn(site_upload.NO_DETAIL, str(error))
                self.assertNotIn("Bad gateway", str(error))

    def test_a_long_detail_is_cut_and_control_characters_are_gone(self) -> None:
        tar_path, sha = self.tar_of(self.make_package())
        server = self.start_server()
        server.answers(422, {"detail": "first\nsecond\x1b[31m\r\x00 " + "x" * 5000})
        text = str(self.refused(self.upload, server, tar_path, sha))
        self.assertLess(len(text), 500)
        self.assertTrue(text.isprintable(), repr(text))
        self.assertIn("first second", text)

    def test_a_detail_that_quotes_the_token_does_not_leak_it(self) -> None:
        tar_path, sha = self.tar_of(self.make_package())
        server = self.start_server()
        server.answers(401, {"detail": f"bad token {TOKEN}"})
        text = str(self.refused(self.upload, server, tar_path, sha))
        self.assertIn("bad token", text)
        self.assertNotIn(TOKEN, text)

    def test_a_200_that_is_no_json_object_is_an_error(self) -> None:
        tar_path, sha = self.tar_of(self.make_package())
        for raw in (b"ok", b"", b"[1]"):
            with self.subTest(raw=raw):
                server = self.start_server()
                server.answers(200, raw=raw)
                self.refused(self.upload, server, tar_path, sha)

    def test_a_redirect_is_not_followed_and_the_token_goes_nowhere_else(self) -> None:
        tar_path, sha = self.tar_of(self.make_package())
        elsewhere = self.start_server()
        server = self.start_server()
        server.answers(301, {"detail": "moved"})
        server.extra_headers["Location"] = f"http://127.0.0.1:{elsewhere.port}/api/v1/builds/{PAGE}"
        error = self.refused(self.upload, server, tar_path, sha)
        self.assertIn("301", str(error))
        self.assertEqual(elsewhere.requests, [])

    def test_an_answer_before_the_body_ends_is_reported_not_a_broken_pipe(self) -> None:
        # A few MB, so the sending cannot finish before the site has answered and closed with the body unread
        tar_path, sha = self.tar_of(self.make_package(), size_hint=24 * site_upload.PIECE_BYTES)
        for _ in range(5):
            server = self.start_server()
            server.mode = "early"
            server.answers(401, {"detail": "wrong token"})
            error = self.refused(self.upload, server, tar_path, sha)
            self.assertIn("401", str(error))
            self.assertIn("wrong token", str(error))

    def test_a_site_that_never_answers_ends_at_the_timeout(self) -> None:
        tar_path, sha = self.tar_of(self.make_package())
        server = self.start_server()
        server.mode = "hang"
        error = self.refused(self.upload, server, tar_path, sha, timeout=0.5)
        self.assertIn("0.5", str(error))
        self.assertLess(0.5, site_upload.UPLOAD_TIMEOUT_SECONDS)

    def test_a_site_that_drops_the_connection_without_an_answer_is_an_error(self) -> None:
        tar_path, sha = self.tar_of(self.make_package(), size_hint=24 * site_upload.PIECE_BYTES)
        server = self.start_server()
        server.mode = "drop"
        error = self.refused(self.upload, server, tar_path, sha)
        self.assertRegex(str(error), "broke|no answer")

    def test_a_site_that_cannot_be_reached_is_an_error(self) -> None:
        tar_path, sha = self.tar_of(self.make_package())
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        connect = lambda host, timeout: http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)  # noqa: E731
        error = self.refused(site_upload.upload, SITE, PAGE, tar_path, sha, TOKEN, connect=connect)
        self.assertIn("connect", str(error))

    def test_bad_arguments_are_refused_before_a_connection_is_made(self) -> None:
        tar_path, sha = self.tar_of(self.make_package())

        def never(host, timeout):
            raise AssertionError("connected")

        cases = {
            "page": (SITE, "Demo/../x", tar_path, sha, TOKEN),
            "sha": (SITE, PAGE, tar_path, "ABC", TOKEN),
            "token": (SITE, PAGE, tar_path, sha, f"{MARKER}\r\nX-Evil: 1"),
            "file": (SITE, PAGE, self.temp / "missing.tar", sha, TOKEN),
        }
        for name, arguments in cases.items():
            with self.subTest(name):
                self.refused(site_upload.upload, *arguments, connect=never)


class ReadTokenTest(SiteUploadTestCase):
    def write_env(self, text: str | bytes, folder: Path | None = None) -> Path:
        folder = folder or self.temp
        path = folder / ".env"
        path.write_bytes(text if isinstance(text, bytes) else text.encode("utf-8"))
        return folder

    def test_the_environment_is_read_first(self) -> None:
        folder = self.write_env(f"{site_upload.ENV_VARIABLE}=from-file\n")
        self.assertEqual(site_upload.read_token(folder, {site_upload.ENV_VARIABLE: "from-environment"}), "from-environment")

    def test_an_empty_environment_value_falls_back_to_the_file(self) -> None:
        folder = self.write_env(f"{site_upload.ENV_VARIABLE}=from-file\n")
        self.assertEqual(site_upload.read_token(folder, {site_upload.ENV_VARIABLE: ""}), "from-file")

    def test_the_file_is_read_with_comments_export_quotes_and_a_bom(self) -> None:
        cases = {
            "plain": "ENGINE_SITE_UPLOAD_TOKEN=abc123\n",
            "comments": "# a comment\n\nOTHER=1\n  # indented comment\nENGINE_SITE_UPLOAD_TOKEN=abc123\n",
            "export": "export ENGINE_SITE_UPLOAD_TOKEN=abc123\n",
            "double quotes": 'ENGINE_SITE_UPLOAD_TOKEN="abc123"\n',
            "single quotes": "ENGINE_SITE_UPLOAD_TOKEN='abc123'\n",
            "inline comment": "ENGINE_SITE_UPLOAD_TOKEN=abc123 # issued today\n",
            "comment after quotes": 'ENGINE_SITE_UPLOAD_TOKEN="abc123"  # issued today\n',
            "spaces around": "  ENGINE_SITE_UPLOAD_TOKEN = abc123  \n",
            "crlf": "OTHER=1\r\nENGINE_SITE_UPLOAD_TOKEN=abc123\r\n",
            "other names are no match": "ENGINE_SITE_UPLOAD_TOKEN_OLD=nope\nXENGINE_SITE_UPLOAD_TOKEN=nope\nENGINE_SITE_UPLOAD_TOKEN=abc123\n",
            "no final newline": "ENGINE_SITE_UPLOAD_TOKEN=abc123",
        }
        for name, text in cases.items():
            with self.subTest(name):
                self.assertEqual(site_upload.read_token(self.write_env(text), {}), "abc123")
        with self.subTest("bom"):
            self.assertEqual(site_upload.read_token(self.write_env(b"\xef\xbb\xbfENGINE_SITE_UPLOAD_TOKEN=abc123\n"), {}), "abc123")

    def test_the_last_value_wins_when_the_name_repeats(self) -> None:
        folder = self.write_env("ENGINE_SITE_UPLOAD_TOKEN=first\nENGINE_SITE_UPLOAD_TOKEN=second\n")
        self.assertEqual(site_upload.read_token(folder, {}), "second")

    def test_a_missing_file_a_missing_name_and_an_empty_value_name_the_variable_and_the_file(self) -> None:
        def folder_with(name: str, text: str | None) -> Path:
            folder = self.temp / name
            folder.mkdir()
            return self.write_env(text, folder) if text is not None else folder

        cases = {
            "no file": folder_with("nowhere", None),
            "no name": folder_with("a", "OTHER=1\n"),
            "empty": folder_with("b", "ENGINE_SITE_UPLOAD_TOKEN=\n"),
            "empty quotes": folder_with("c", 'ENGINE_SITE_UPLOAD_TOKEN=""\n'),
        }
        for name, folder in cases.items():
            with self.subTest(name):
                error = self.refused(site_upload.read_token, folder, {})
                self.assertIn(site_upload.ENV_VARIABLE, str(error))
                self.assertIn(str(folder / ".env"), str(error))

    def test_an_unusable_value_is_refused_without_quoting_it(self) -> None:
        values = {
            "space": f"{MARKER} with space",
            "tab": f"{MARKER}\tx",
            "control": f"{MARKER}\x01x",
            "non ascii": f"{MARKER}é",
            "too long": MARKER + "a" * 600,
        }
        for name, value in values.items():
            with self.subTest(name, source="environment"):
                error = self.refused(site_upload.read_token, self.temp, {site_upload.ENV_VARIABLE: value})
                self.assertIn(site_upload.ENV_VARIABLE, str(error))
                self.assertIn("environment", str(error))
        for name, value in values.items():
            if "\t" in value or "\x01" in value:
                continue
            with self.subTest(name, source="file"):
                folder = self.temp / f"f-{name.replace(' ', '-')}"
                folder.mkdir()
                self.write_env(f"{site_upload.ENV_VARIABLE}={value}\n", folder)
                error = self.refused(site_upload.read_token, folder, {})
                self.assertIn(site_upload.ENV_VARIABLE, str(error))
                self.assertIn(str(folder / ".env"), str(error))

    def test_a_quote_that_does_not_close_is_refused_without_quoting_it(self) -> None:
        for text in (f'ENGINE_SITE_UPLOAD_TOKEN="{MARKER}\n', f'ENGINE_SITE_UPLOAD_TOKEN="{MARKER}" trailing\n'):
            with self.subTest(text):
                error = self.refused(site_upload.read_token, self.write_env(text), {})
                self.assertIn("line 1", str(error))

    def test_a_file_that_is_not_utf8_is_refused(self) -> None:
        error = self.refused(site_upload.read_token, self.write_env(b"ENGINE_SITE_UPLOAD_TOKEN=\xff\xfe\n"), {})
        self.assertIn(site_upload.ENV_VARIABLE, str(error))

    def test_the_process_environment_is_the_default_source(self) -> None:
        from unittest import mock
        with mock.patch.dict(os.environ, {site_upload.ENV_VARIABLE: "from-process"}):
            self.assertEqual(site_upload.read_token(self.temp / "no-such-folder"), "from-process")


if __name__ == "__main__":
    unittest.main()
