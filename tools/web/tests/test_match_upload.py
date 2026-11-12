"""Tests of the /matches/ endpoint of tools/web/serve.py (the receiving side): a real server with a temp match store.
Run: python -m unittest discover tools/web/tests"""

import contextlib
import http.client
import io
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

from test_serve import ServerTestCase, basic  # first: it puts tools/web on the path

import serve

MATCH_ID = "0123456789abcdef0123456789abcdef"
OTHER_ID = "fedcba9876543210fedcba9876543210"
START = b'{"event":"start","version":5,"seat":"East"}\n'
SETTLEMENT = b'{"event":"settlement","points":[1,2,3,4]}\n{"event":"end"}\n'
PIECE_LIMIT = 1 << 20


def piece_path(id_: str = MATCH_ID, index: int | str = 0) -> str:
    return f"/matches/{id_}/{index}"


class MatchStoreTestCase(ServerTestCase):
    versions = (5, 5)
    store_options: dict = {}

    def server_options(self, root: Path) -> dict:
        self.store = root / "store"
        return dict(match_store=self.store, match_versions=self.versions, **self.store_options)

    def post(self, path: str, body: bytes, headers: dict | None = None):
        return self.request(path, method="POST", headers=headers, body=body)

    def stored(self) -> dict[str, bytes]:
        """Everything in the store folder: relative name -> bytes."""
        return {file.relative_to(self.store).as_posix(): file.read_bytes() for file in self.store.rglob("*") if file.is_file()}

    def exchange(self, data: bytes) -> bytes:
        """Raw bytes to the server and everything it answers until it closes the connection (a timeout means it kept it open)."""
        with socket.create_connection(("127.0.0.1", self.port), timeout=10) as connection:
            connection.sendall(data)
            received = b""
            while True:
                chunk = connection.recv(65536)
                if not chunk:
                    return received
                received += chunk


class ReceivingTest(MatchStoreTestCase):
    def test_a_piece_is_stored_byte_for_byte_and_answered_200(self):
        body = START + b'{"event":"deal","tiles":"\xc3\xa4\xe2\x82\xac"}\n'
        response = self.post(piece_path(), body)
        self.assertEqual((response.status, response.body), (200, b"stored"))
        self.assertEqual(self.stored(), {f"{MATCH_ID}/0.jsonl": body})
        self.assertEqual(response.headers["cache-control"], "no-store")

    def test_a_match_in_pieces_goes_to_one_folder(self):
        self.assertEqual(self.post(piece_path(index=0), START).status, 200)
        self.assertEqual(self.post(piece_path(index=1), SETTLEMENT).status, 200)
        self.assertEqual(self.post(piece_path(index=12), SETTLEMENT).status, 200)
        self.assertEqual(sorted(self.stored()), [f"{MATCH_ID}/{index}.jsonl" for index in (0, 1, 12)])

    def test_a_bad_path_is_400_and_nothing_is_stored(self):
        for path in (f"/matches/{MATCH_ID.upper()}/0", f"/matches/{MATCH_ID[:31]}/0", f"/matches/{MATCH_ID}0/0",
                     "/matches/" + "g" * 32 + "/0", f"/matches/{MATCH_ID}/-1", f"/matches/{MATCH_ID}/a", f"/matches/{MATCH_ID}/1.jsonl",
                     f"/matches/{MATCH_ID}/", f"/matches/{MATCH_ID}", "/matches/", "/matches", f"/matches/{MATCH_ID}/01",
                     f"/matches/{MATCH_ID}/1234567", f"/matches/{MATCH_ID}/+1", f"/matches/{MATCH_ID}/0/0", f"/matches/{MATCH_ID}/0/",
                     f"/matches/{MATCH_ID}/%30", f"/matches/{MATCH_ID[:30]}%61%61/0", f"/matches/{MATCH_ID}/%D9%A1", "/matches/../0",
                     f"/matches/{MATCH_ID}/../{OTHER_ID}/0", f"/matches/..%2f{MATCH_ID}/0", f"/matches//0", f"/matches/{MATCH_ID}//0"):
            with self.subTest(path):
                response = self.post(path, START)
                self.assertEqual(response.status, 400)
                self.assertNotIn(MATCH_ID.encode(), response.body)
        self.assertEqual(self.stored(), {})
        self.assertEqual(list(self.store.iterdir()), [])

    def test_the_largest_piece_is_stored_and_one_byte_more_is_413_before_the_body_is_read(self):
        head = START
        padding = PIECE_LIMIT - len(head) - len(b'{"event":"pad","text":""}\n')
        body = head + b'{"event":"pad","text":"' + b"x" * padding + b'"}\n'
        self.assertEqual(len(body), PIECE_LIMIT)
        self.assertEqual(self.post(piece_path(), body).status, 200)
        # only the head is sent: a server that read the body first would wait for it
        received = self.exchange(f"POST {piece_path(OTHER_ID)} HTTP/1.1\r\nHost: x\r\nContent-Length: {PIECE_LIMIT + 1}\r\n\r\n".encode())
        self.assertTrue(received.startswith(b"HTTP/1.1 413"), received)
        self.assertEqual(list(self.stored()), [f"{MATCH_ID}/0.jsonl"])

    def test_a_body_that_breaks_a_rule_is_400_and_nothing_is_stored(self):
        for name, body in (("not UTF-8", START + b'{"event":"x","text":"\xff"}\n'),
                           ("an array", START + b"[1]\n"), ("a number", START + b"5\n"), ("a string", START + b'"x"\n'),
                           ("null", START + b"null\n"), ("bad JSON", START + b'{"event":\n'), ("text", START + b"hello\n"),
                           ("no final newline", START + b'{"event":"x"}'), ("a blank line inside", START + b"\n" + SETTLEMENT),
                           ("a blank line at the end", START + b"\n"), ("a bare CR", START + b"\r\n"),
                           ("deeply nested", START + b"[" * 100000 + b"]" * 100000 + b"\n"), ("only a newline", b"\n")):
            with self.subTest(name):
                self.assertEqual(self.post(piece_path(), body).status, 400)
        self.assertEqual(self.stored(), {})

    def test_the_first_piece_starts_with_the_start_event_of_an_accepted_version(self):
        for name, body in (("empty", b""), ("another event", b'{"event":"deal","version":5}\n'),
                           ("start after another line", b'{"event":"deal"}\n' + START), ("no version", b'{"event":"start"}\n'),
                           ("an older version", b'{"event":"start","version":4}\n'), ("a newer version", b'{"event":"start","version":6}\n'),
                           ("a string version", b'{"event":"start","version":"5"}\n'), ("a float version", b'{"event":"start","version":5.0}\n'),
                           ("a boolean version", b'{"event":"start","version":true}\n'), ("a null version", b'{"event":"start","version":null}\n'),
                           ("event not a string", b'{"event":["start"],"version":5}\n')):
            with self.subTest(name):
                self.assertEqual(self.post(piece_path(), body).status, 400)
        self.assertEqual(self.stored(), {})
        self.assertEqual(self.post(piece_path(), b'{"version":5,"event":"start"}\n').status, 200)

    def test_a_later_piece_needs_no_start_and_may_be_empty(self):
        self.assertEqual(self.post(piece_path(index=1), SETTLEMENT).status, 200)
        self.assertEqual(self.post(piece_path(index=2), b"").status, 200)
        self.assertEqual(self.stored(), {f"{MATCH_ID}/1.jsonl": SETTLEMENT, f"{MATCH_ID}/2.jsonl": b""})

    def test_the_same_bytes_again_are_200_other_bytes_are_409_and_the_file_stays(self):
        self.assertEqual(self.post(piece_path(), START).status, 200)
        file = self.store / MATCH_ID / "0.jsonl"
        os.utime(file, (1_000_000, 1_000_000))
        again = self.post(piece_path(), START)
        self.assertEqual((again.status, again.body), (200, b"already stored"))
        other = self.post(piece_path(), START + b'{"event":"x"}\n')
        self.assertEqual(other.status, 409)
        self.assertEqual(file.read_bytes(), START)
        self.assertEqual(file.stat().st_mtime, 1_000_000)
        self.assertEqual(self.post(piece_path(index=3), b"").status, 200)
        self.assertEqual(self.post(piece_path(index=3), b"").status, 200)
        self.assertEqual(self.post(piece_path(index=3), SETTLEMENT).status, 409)

    def test_equal_posts_at_the_same_time_are_all_200_and_leave_one_whole_file(self):
        body = START + b'{"event":"pad","text":"' + b"y" * 200_000 + b'"}\n'
        barrier = threading.Barrier(8)
        statuses = []

        def worker():
            barrier.wait()
            statuses.append(self.post(piece_path(), body).status)

        threads = [threading.Thread(target=worker) for _ in range(8)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=30)
        self.assertEqual(statuses, [200] * 8)
        self.assertEqual(self.stored(), {f"{MATCH_ID}/0.jsonl": body})

    def test_different_posts_at_the_same_time_leave_one_winner_and_409_for_the_rest(self):
        bodies = [START + b'{"event":"x","n":%d}\n' % number for number in range(6)]
        barrier = threading.Barrier(len(bodies))
        statuses = []

        def worker(body):
            barrier.wait()
            statuses.append(self.post(piece_path(), body).status)

        threads = [threading.Thread(target=worker, args=(body,)) for body in bodies]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=30)
        self.assertEqual(sorted(statuses), [200] + [409] * 5)
        self.assertIn(self.stored()[f"{MATCH_ID}/0.jsonl"], bodies)

    def test_nothing_but_pieces_is_left_after_refusals_and_successes(self):
        self.post(piece_path(), START)
        self.post(piece_path(), START + b"x\n")
        self.post(piece_path(index=1), b"nonsense")
        self.post(piece_path(OTHER_ID), b"nonsense")
        self.assertEqual(sorted(path.name for path in self.store.rglob("*")), ["0.jsonl", MATCH_ID])

    def test_a_failed_write_leaves_no_temp_file_and_no_empty_folder(self):
        with mock.patch("serve.os.replace", side_effect=OSError("disk full")):
            response = self.post(piece_path(), START)
        self.assertEqual(response.status, 507)
        self.assertEqual(list(self.store.rglob("*")), [])
        self.assertEqual(self.post(piece_path(), START).status, 200)

    def test_the_replies_are_short_text_without_the_input(self):
        marker = "needle-" + "z" * 20
        response = self.post(piece_path(), ('{"event":"start","version":5,"x":"%s"}' % marker).encode())
        self.assertEqual(response.status, 400)
        self.assertNotIn(marker.encode(), response.body)
        self.assertLess(len(response.body), 80)
        self.assertEqual(response.headers["content-type"], "text/plain; charset=utf-8")

    def test_a_connection_is_kept_after_a_stored_piece_and_after_a_refused_body(self):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            for index, body, expected in ((0, START, 200), (1, b"nonsense", 400), (1, SETTLEMENT, 200)):
                connection.request("POST", piece_path(index=index), body=body)
                response = connection.getresponse()
                response.read()
                self.assertEqual(response.status, expected)
        finally:
            connection.close()


class BodyFramingTest(MatchStoreTestCase):
    def raw_post(self, headers: str, body: bytes = b"") -> bytes:
        return self.exchange(f"POST {piece_path()} HTTP/1.1\r\nHost: x\r\n{headers}\r\n".encode() + body)

    def test_no_content_length_is_411_and_the_connection_closes(self):
        received = self.raw_post("")
        self.assertTrue(received.startswith(b"HTTP/1.1 411"), received)
        self.assertIn(b"Connection: close", received)

    def test_a_chunked_body_is_411(self):
        received = self.raw_post("Transfer-Encoding: chunked\r\n", b"2a\r\n" + START + b"\r\n0\r\n\r\n")
        self.assertTrue(received.startswith(b"HTTP/1.1 411"), received)
        self.assertEqual(received.count(b"HTTP/1."), 1, received)
        self.assertEqual(self.stored(), {})

    def test_a_content_length_that_is_not_one_number_is_400(self):
        for headers in ("Content-Length: abc\r\n", "Content-Length: -5\r\n", "Content-Length: 1e3\r\n", "Content-Length: 5\r\nContent-Length: 5\r\n",
                        "Content-Length: 99999999999999999999\r\n"):
            with self.subTest(headers=headers):
                received = self.raw_post(headers, START)
                self.assertTrue(received.startswith(b"HTTP/1.1 400"), received)
                self.assertEqual(received.count(b"HTTP/1."), 1, received)
        self.assertEqual(self.stored(), {})

    def test_a_body_cut_short_stores_nothing_and_the_server_goes_on(self):
        with socket.create_connection(("127.0.0.1", self.port), timeout=10) as connection:
            connection.sendall(f"POST {piece_path()} HTTP/1.1\r\nHost: x\r\nContent-Length: 500\r\n\r\n".encode() + START)
            connection.shutdown(socket.SHUT_WR)
            self.assertEqual(connection.recv(65536), b"")
        self.assertEqual(self.stored(), {})
        self.assertEqual(self.post(piece_path(), START).status, 200)

    def test_extra_bytes_after_the_body_are_not_taken_for_the_body(self):
        received = self.raw_post(f"Content-Length: {len(START)}\r\nConnection: close\r\n", START + b"garbage")
        self.assertTrue(received.startswith(b"HTTP/1.1 200"), received)
        self.assertEqual(self.stored(), {f"{MATCH_ID}/0.jsonl": START})


class RoutingWithAStoreTest(MatchStoreTestCase):
    def test_static_files_and_healthz_are_as_before(self):
        self.assertEqual(self.request("/").status, 200)
        self.assertEqual(self.request("/healthz").body, b"ok")
        self.assertEqual(self.request("/nothing").status, 404)
        response = self.request("/index.html", method="POST")
        self.assertEqual((response.status, response.headers["allow"]), (405, "GET, HEAD"))
        self.assertEqual(self.post("/healthz", START).status, 405)

    def test_other_methods_under_matches_are_405_and_the_allow_names_the_four(self):
        for method in ("PUT", "PATCH", "OPTIONS", "TRACE", "FOO"):
            with self.subTest(method):
                response = self.request(piece_path(), method=method)
                self.assertEqual((response.status, response.headers["allow"], response.headers["connection"]),
                                 (405, "GET, HEAD, POST, DELETE", "close"))
        self.assertEqual(self.stored(), {})

    def test_the_path_is_not_decoded_so_an_encoded_slash_is_no_separator(self):
        self.assertEqual(self.post(f"/matches%2f{MATCH_ID}/0", START).status, 405)
        self.assertEqual(self.stored(), {})


class WithoutAStoreTest(ServerTestCase):
    def test_the_answers_are_those_of_a_static_server(self):
        for method, status in (("POST", 405), ("DELETE", 405), ("GET", 404), ("HEAD", 404), ("PUT", 405)):
            with self.subTest(method):
                response = self.request(piece_path(), method=method)
                self.assertEqual(response.status, status)
                if status == 405:
                    self.assertEqual(response.headers["allow"], "GET, HEAD")
        self.assertEqual(self.request("/matches/").status, 404)
        self.assertEqual(self.request("/").status, 200)

    def test_a_folder_named_matches_in_the_served_files_is_served_as_any_other(self):
        (self.dist / "matches").mkdir()
        (self.dist / "matches" / "note.txt").write_bytes(b"static")
        self.assertEqual(self.request("/matches/note.txt").body, b"static")


class MatchAuthTest(MatchStoreTestCase):
    basic_auth = ("tester", "s3cret")

    def test_a_post_without_credentials_is_401_nothing_is_stored_and_the_connection_closes(self):
        for headers in ({}, basic("tester", "wrong")):
            with self.subTest(headers=headers):
                response = self.post(piece_path(), START, headers)
                self.assertEqual(response.status, 401)
                self.assertTrue(response.headers["www-authenticate"].startswith("Basic realm="))
                self.assertEqual(response.headers["connection"], "close")
        self.assertEqual(self.stored(), {})

    def test_a_post_with_credentials_is_stored(self):
        self.assertEqual(self.post(piece_path(), START, basic(*self.basic_auth)).status, 200)
        self.assertEqual(list(self.stored()), [f"{MATCH_ID}/0.jsonl"])

    def test_a_bad_path_without_credentials_is_401_not_400(self):
        self.assertEqual(self.post("/matches/x", START).status, 401)
        self.assertEqual(self.request("/matches/x", method="PUT").status, 401)


class AccessLogTest(MatchStoreTestCase):
    def server_options(self, root: Path) -> dict:
        return dict(super().server_options(root), quiet=False)

    def test_the_log_has_neither_the_id_nor_the_body_but_does_log_other_requests(self):
        marker = "needle-body-marker"
        log = io.StringIO()
        with contextlib.redirect_stderr(log):
            self.post(piece_path(), START)
            self.post(piece_path(index=1), marker.encode())
            self.post(piece_path(), START + b"x\n")
            self.post("/matches/" + OTHER_ID, START)
            self.request(piece_path(), method="GET")
            self.exchange(f"POST {piece_path(OTHER_ID)} HTTP/1.1 junk junk\r\n\r\n".encode())
            self.request("/nothing")
        text = log.getvalue()
        self.assertIn("/nothing", text)
        for secret in (MATCH_ID, OTHER_ID, marker, "/matches"):
            self.assertNotIn(secret, text)


class ConfigurationTest(unittest.TestCase):
    def parse(self, **environ):
        return serve.parse_args([], environ=environ)

    def test_the_defaults_are_a_store_that_is_off_and_a_gigabyte(self):
        args = self.parse()
        self.assertEqual((args.match_store, args.match_versions, args.match_store_max_bytes), (None, None, 1024 << 20))

    def test_the_variables_are_read(self):
        args = self.parse(MATCH_STORE="/data/matches", MATCH_STORE_MAX_MB="64", MATCH_LOG_VERSIONS="1-5")
        self.assertEqual((args.match_store, args.match_versions, args.match_store_max_bytes), (Path("/data/matches"), (1, 5), 64 << 20))
        self.assertEqual(self.parse(MATCH_LOG_VERSIONS=" 5 ").match_versions, (5, 5))
        self.assertIsNone(self.parse(MATCH_STORE="", MATCH_LOG_VERSIONS="", MATCH_STORE_MAX_MB="").match_store)

    def test_a_bad_value_does_not_start(self):
        for environ in ({"MATCH_STORE_MAX_MB": value} for value in ("0", "-1", "abc", "1.5", "1e3", "99999999999")):
            with self.subTest(environ=environ), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                self.parse(**environ)
        for environ in ({"MATCH_LOG_VERSIONS": value} for value in ("5-1", "a", "1-", "-5", "1-2-3", "0x5", "1,5", "9999999")):
            with self.subTest(environ=environ), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                self.parse(**environ)

    def test_main_stops_on_a_bad_value(self):
        for variable, value in (("MATCH_STORE_MAX_MB", "lots"), ("MATCH_LOG_VERSIONS", "5-1")):
            with self.subTest(variable), mock.patch.dict(os.environ, {variable: value}), \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                serve.main(["--root", "."])


class StartTest(unittest.TestCase):
    def test_a_store_that_cannot_be_written_stops_the_start(self):
        with tempfile.TemporaryDirectory() as temp:
            blocker = Path(temp) / "file"
            blocker.write_bytes(b"x")
            with self.assertRaisesRegex(serve.MatchStoreError, "match store is not writable"):
                serve.make_server(Path(temp), "127.0.0.1", 0, match_store=blocker / "matches")
            errors = io.StringIO()
            with mock.patch.dict(os.environ, {"MATCH_STORE": str(blocker / "matches")}), contextlib.redirect_stderr(errors):
                self.assertEqual(serve.main(["--root", temp, "--host", "127.0.0.1", "--port", "0"]), 1)
            self.assertIn("match store is not writable", errors.getvalue())

    def start_line(self, **environ) -> str:
        with tempfile.TemporaryDirectory() as temp:
            dist = Path(temp) / "dist"
            dist.mkdir()
            variables = {key: value for key, value in os.environ.items() if not key.startswith(("MATCH_", "BASIC_AUTH"))}
            variables.update(PORT="0", **{key: value.replace("<temp>", temp) for key, value in environ.items()})
            process = subprocess.Popen([sys.executable, serve.__file__, "--root", str(dist), "--host", "127.0.0.1", "--quiet"],
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=variables, text=True)
            watchdog = threading.Timer(30, process.kill)
            watchdog.start()
            try:
                return process.stdout.readline()
            finally:
                watchdog.cancel()
                process.kill()
                process.wait(timeout=10)
                process.stdout.close()

    def test_the_start_line_names_the_store_or_says_off(self):
        self.assertIn("match store: off", self.start_line())
        line = self.start_line(MATCH_STORE="<temp>/matches", MATCH_LOG_VERSIONS="1-5")
        self.assertIn("match store: ", line)
        self.assertIn("matches", line)
        self.assertNotIn("WARNING", line)

    def test_a_store_without_a_version_range_warns_on_the_start_line(self):
        line = self.start_line(MATCH_STORE="<temp>/matches")
        self.assertIn("WARNING", line)
        self.assertIn("MATCH_LOG_VERSIONS", line)

    def test_the_store_folder_is_created_at_start(self):
        with tempfile.TemporaryDirectory() as temp:
            serve.make_server(Path(temp), "127.0.0.1", 0, match_store=Path(temp) / "a" / "b").server_close()
            self.assertTrue((Path(temp) / "a" / "b").is_dir())


class PieceRulesTest(unittest.TestCase):
    def test_check_piece_names_a_reason_and_none_when_fine(self):
        self.assertIsNone(serve.check_piece(START, 0, (5, 5)))
        self.assertIsNone(serve.check_piece(START, 0, (1, 5)))
        self.assertIsNone(serve.check_piece(b"", 4, None))
        self.assertIsNotNone(serve.check_piece(START, 0, None))
        self.assertIsNotNone(serve.check_piece(b"", 0, (5, 5)))
        self.assertIsNone(serve.check_piece(json.dumps({"event": "end"}).encode() + b"\n", 7, None))


if __name__ == "__main__":
    unittest.main()
