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
DAY = 24 * 3600
START_TIME = 1_700_000_000.0


def piece_path(id_: str = MATCH_ID, index: int | str = 0) -> str:
    return f"/matches/{id_}/{index}"


class FakeClock:
    """The clock of the server: the tests move it by hand, so nothing sleeps."""

    def __init__(self, now: float = START_TIME):
        self.now = now

    def __call__(self) -> float:
        return self.now


class MatchStoreTestCase(ServerTestCase):
    versions = (5, 5)
    store_options: dict = {}

    def server_options(self, root: Path) -> dict:
        self.store = root / "store"
        self.clock = FakeClock()
        return dict(match_store=self.store, match_versions=self.versions, clock=self.clock, **self.store_options)

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
            self.request("/matches/")
            self.request(f"/matches/{MATCH_ID}", method="DELETE")
            self.exchange(f"POST {piece_path(OTHER_ID)} HTTP/1.1 junk junk\r\n\r\n".encode())
            self.request("/nothing")
        text = log.getvalue()
        self.assertIn("/nothing", text)
        for secret in (MATCH_ID, OTHER_ID, marker, "/matches"):
            self.assertNotIn(secret, text)


class RateLimitTest(MatchStoreTestCase):
    def post_ok(self):
        return self.post(piece_path(), START)

    def test_sixty_posts_an_hour_pass_and_the_next_is_429_with_retry_after(self):
        for _ in range(60):
            self.assertEqual(self.post_ok().status, 200)
        response = self.post_ok()
        self.assertEqual((response.status, response.headers["retry-after"], response.headers["connection"]), (429, "3600", "close"))
        self.clock.now += 1800
        self.assertEqual(self.post_ok().headers["retry-after"], "1800")
        self.assertEqual(self.stored(), {f"{MATCH_ID}/0.jsonl": START})

    def test_the_window_slides(self):
        self.assertEqual(self.post_ok().status, 200)
        self.clock.now += 1000
        for _ in range(59):
            self.assertEqual(self.post_ok().status, 200)
        self.assertEqual(self.post_ok().status, 429)
        self.clock.now = START_TIME + 3600  # the first one is out of the window, the others are not
        self.assertEqual(self.post_ok().status, 200)
        response = self.post_ok()
        self.assertEqual((response.status, response.headers["retry-after"]), (429, "1000"))

    def test_every_post_with_a_good_path_counts_a_bad_path_does_not(self):
        for _ in range(100):
            self.assertEqual(self.post("/matches/nope/0", START).status, 400)
        for _ in range(60):
            self.assertEqual(self.post(piece_path(), b"nonsense").status, 400)
        self.assertEqual(self.post(piece_path(), START).status, 429)
        self.assertEqual(self.stored(), {})

    def test_reading_and_deleting_are_not_limited(self):
        for _ in range(60):
            self.post_ok()
        self.assertEqual(self.request("/matches/").status, 200)
        self.assertEqual(self.request(piece_path() + ".jsonl").status, 200)
        self.assertEqual(self.request(f"/matches/{MATCH_ID}", method="DELETE").status, 200)

    def test_the_forwarded_address_is_ignored_unless_the_switch_is_on(self):
        for number in range(60):
            self.assertEqual(self.post(piece_path(), START, {"X-Forwarded-For": f"10.0.0.{number}"}).status, 200)
        self.assertEqual(self.post(piece_path(), START, {"X-Forwarded-For": "10.1.1.1"}).status, 429)


class ForwardedLimitTest(MatchStoreTestCase):
    store_options = {"match_forwarded_for": True}

    def forwarded(self, value: str):
        return self.post(piece_path(), START, {"X-Forwarded-For": value})

    def test_the_last_entry_is_the_client_and_another_address_is_free(self):
        for _ in range(60):
            self.assertEqual(self.forwarded("6.6.6.6, 10.0.0.1").status, 200)
        self.assertEqual(self.forwarded("10.0.0.1").status, 429)
        self.assertEqual(self.forwarded("7.7.7.7, 10.0.0.1").status, 429)  # what the client wrote before the edge does not count
        self.assertEqual(self.forwarded("10.0.0.2").status, 200)
        self.assertEqual(self.forwarded("2001:db8::1").status, 200)
        self.assertEqual(self.forwarded("2001:0db8:0:0:0:0:0:1").status, 200)

    def test_a_missing_or_malformed_header_counts_for_the_peer(self):
        for _ in range(30):
            self.assertEqual(self.post(piece_path(), START).status, 200)
        for _ in range(30):
            self.assertEqual(self.forwarded("not an address").status, 200)
        self.assertEqual(self.post(piece_path(), START).status, 429)
        self.assertEqual(self.forwarded("10.0.0.9").status, 200)


class RateLimiterTest(unittest.TestCase):
    def test_keys_are_independent_and_the_wait_is_whole_seconds(self):
        clock = FakeClock(100.0)
        limiter = serve.RateLimiter(2, 10, clock)
        self.assertEqual([limiter.check("a"), limiter.check("a"), limiter.check("b")], [0, 0, 0])
        self.assertEqual(limiter.check("a"), 10)
        clock.now = 104.5
        self.assertEqual(limiter.check("a"), 6)
        clock.now = 110.0
        self.assertEqual([limiter.check("a"), limiter.check("a")], [0, 0])

    def test_the_memory_is_bounded(self):
        clock = FakeClock(0.0)
        limiter = serve.RateLimiter(5, 3600, clock, max_keys=100)
        for number in range(1000):
            self.assertEqual(limiter.check(f"key{number}"), 0)
            self.assertLessEqual(len(limiter._times), 100)
        clock.now = 7200  # every key is out of its window: the next new key clears them
        limiter.check("late")
        self.assertEqual(list(limiter._times), ["late"])

    def test_a_key_that_posts_a_lot_holds_at_most_the_limit_in_memory(self):
        limiter = serve.RateLimiter(3, 3600, FakeClock())
        for _ in range(1000):
            limiter.check("a")
        self.assertEqual(len(limiter._times["a"]), 3)


class CapTest(MatchStoreTestCase):
    store_options = {"match_store_max_bytes": 100}

    def test_a_new_piece_over_the_cap_is_507_and_not_stored(self):
        self.assertEqual(self.post(piece_path(index=0), START).status, 200)
        self.assertEqual(self.post(piece_path(index=1), START).status, 200)
        response = self.post(piece_path(index=2), START)
        self.assertEqual(response.status, 507)
        self.assertEqual(sorted(self.stored()), [f"{MATCH_ID}/0.jsonl", f"{MATCH_ID}/1.jsonl"])

    def test_a_repeat_of_a_stored_piece_is_200_even_when_the_store_is_full(self):
        self.post(piece_path(index=0), START)
        self.post(piece_path(index=1), START)
        self.assertEqual(self.post(piece_path(index=2), START).status, 507)
        self.assertEqual(self.post(piece_path(index=0), START).status, 200)
        self.assertEqual(self.post(piece_path(index=1), START).status, 200)
        self.assertEqual(self.post(piece_path(index=1), START + b"{}\n").status, 409)

    def test_a_piece_of_no_bytes_fits_and_a_delete_makes_room(self):
        self.post(piece_path(index=0), START)
        self.post(piece_path(index=1), START)
        self.assertEqual(self.post(piece_path(index=5), b"").status, 200)
        self.assertEqual(self.post(piece_path(OTHER_ID, 1), START).status, 507)
        self.assertEqual(self.request(f"/matches/{MATCH_ID}", method="DELETE").status, 200)
        self.assertEqual(self.post(piece_path(OTHER_ID, 1), START).status, 200)

    def test_the_size_is_counted_again_at_start(self):
        store = serve.MatchStore(self.store, None, 100)
        self.assertEqual(store.put(OTHER_ID, 1, START)[0], 200)
        self.assertEqual(store.put(OTHER_ID, 2, START)[0], 200)
        restarted = serve.MatchStore(self.store, None, 100)
        self.assertEqual(restarted.put(MATCH_ID, 1, START)[0], 507)
        self.assertEqual(restarted.put(OTHER_ID, 1, START)[0], 200)


class ReadingTest(MatchStoreTestCase):
    def send_match(self, id_: str = MATCH_ID):
        pieces = [START, SETTLEMENT, b"", b'{"event":"end","text":"\xc3\xa4"}\n']
        for index, body in enumerate(pieces):
            self.assertEqual(self.post(piece_path(id_, index), body).status, 200)
        return pieces

    def test_an_empty_store_lists_no_matches(self):
        response = self.request("/matches/")
        self.assertEqual((response.status, response.headers["content-type"], response.headers["cache-control"]),
                         (200, "application/json", "no-store"))
        self.assertEqual(json.loads(response.body), {"matches": []})

    def test_the_list_is_sorted_and_counts_the_pieces(self):
        self.send_match(OTHER_ID)
        self.assertEqual(self.post(piece_path(MATCH_ID, 0), START).status, 200)
        self.assertEqual(json.loads(self.request("/matches/").body),
                         {"matches": [{"id": MATCH_ID, "pieces": 1}, {"id": OTHER_ID, "pieces": 4}]})

    def test_a_match_sent_in_pieces_is_read_back_byte_for_byte_empty_piece_included(self):
        pieces = self.send_match()
        for index, body in enumerate(pieces):
            with self.subTest(index):
                response = self.request(piece_path(index=f"{index}.jsonl"))
                self.assertEqual((response.status, response.body), (200, body))
                self.assertEqual((response.headers["content-type"], response.headers["cache-control"]),
                                 ("application/x-ndjson", "no-store"))
                self.assertEqual(int(response.headers["content-length"]), len(body))

    def test_head_has_the_headers_and_no_body(self):
        self.send_match()
        response = self.request(piece_path(index="1.jsonl"), method="HEAD")
        self.assertEqual((response.status, response.body, int(response.headers["content-length"])), (200, b"", len(SETTLEMENT)))
        self.assertEqual(self.request("/matches/", method="HEAD").body, b"")
        self.assertEqual(self.request(piece_path(index="9.jsonl"), method="HEAD").status, 404)

    def test_a_missing_piece_and_a_malformed_name_are_404(self):
        self.send_match()
        for path in (piece_path(index="9.jsonl"), piece_path(OTHER_ID, "0.jsonl"), piece_path(index=0), piece_path(index="0.json"),
                     piece_path(index="01.jsonl"), piece_path(index="-1.jsonl"), piece_path(index=".jsonl"), piece_path(index="1.jsonl/x"),
                     piece_path(MATCH_ID.upper(), "0.jsonl"), f"/matches/{MATCH_ID}", f"/matches/{MATCH_ID}/", "/matches",
                     "/matches/x/y/z", "/matches/../index.html", f"/matches/{MATCH_ID}/..%2f..%2findex.html", f"/matches/{MATCH_ID}/%30.jsonl",
                     f"/matches/{MATCH_ID}/0.jsonl%00", "/matches//"):
            with self.subTest(path):
                self.assertEqual(self.request(path).status, 404)

    def test_strangers_are_not_listed_and_leftovers_are_never_served(self):
        self.send_match()
        (self.store / "junk").mkdir()
        (self.store / "junk" / "0.jsonl").write_bytes(b"x")
        (self.store / "stray.txt").write_bytes(b"x")
        (self.store / OTHER_ID).mkdir()
        (self.store / MATCH_ID / ".tmp-abc").write_bytes(b"half")
        (self.store / MATCH_ID / "notes.txt").write_bytes(b"x")
        (self.store / MATCH_ID / "7.json").write_bytes(b"x")
        self.assertEqual(json.loads(self.request("/matches/").body), {"matches": [{"id": MATCH_ID, "pieces": 4}]})
        for path in (f"/matches/{MATCH_ID}/.tmp-abc", f"/matches/{MATCH_ID}/.tmp-abc.jsonl", f"/matches/{MATCH_ID}/notes.txt",
                     f"/matches/{OTHER_ID}/0.jsonl", "/matches/junk/0.jsonl"):
            with self.subTest(path):
                self.assertEqual(self.request(path).status, 404)


class IsolatedReadingTest(MatchStoreTestCase):
    cross_origin_isolation = True

    def test_the_isolation_headers_are_on_every_answer(self):
        self.post(piece_path(), START)
        for path, method in (("/matches/", "GET"), (piece_path(index="0.jsonl"), "GET"), (piece_path(index="0.jsonl"), "HEAD"),
                             (piece_path(index="5.jsonl"), "GET"), (f"/matches/{MATCH_ID}", "DELETE"), (f"/matches/{MATCH_ID}", "DELETE"),
                             (piece_path(index=0), "POST"), (piece_path(index=0), "PUT")):
            with self.subTest(path=path, method=method):
                headers = self.request(path, method=method, body=START if method == "POST" else None).headers
                self.assertEqual(headers["cross-origin-opener-policy"], "same-origin")
                self.assertEqual(headers["cross-origin-embedder-policy"], "require-corp")


class DeleteTest(MatchStoreTestCase):
    def delete(self, path: str):
        return self.request(path, method="DELETE")

    def test_a_stored_match_is_removed_and_then_gone(self):
        self.post(piece_path(index=0), START)
        self.post(piece_path(index=1), SETTLEMENT)
        self.post(piece_path(OTHER_ID, 0), START)
        response = self.delete(f"/matches/{MATCH_ID}")
        self.assertEqual((response.status, response.headers["connection"]), (200, "close"))
        self.assertEqual(sorted(self.stored()), [f"{OTHER_ID}/0.jsonl"])
        self.assertEqual(self.delete(f"/matches/{MATCH_ID}").status, 404)
        self.assertEqual(self.request(piece_path(index="0.jsonl")).status, 404)
        self.assertEqual(json.loads(self.request("/matches/").body), {"matches": [{"id": OTHER_ID, "pieces": 1}]})

    def test_a_malformed_id_is_400_and_nothing_is_touched(self):
        self.post(piece_path(index=0), START)
        outside = self.store.parent / "outside.txt"
        outside.write_bytes(b"keep")
        for path in ("/matches/..", "/matches/..%2f..", "/matches/..%2fstore", f"/matches/{MATCH_ID.upper()}", f"/matches/{MATCH_ID[:31]}",
                     "/matches/.", "/matches/%2e%2e", "/matches/*", f"/matches/{MATCH_ID}%00"):
            with self.subTest(path):
                self.assertEqual(self.delete(path).status, 400)
        self.assertEqual(self.stored(), {f"{MATCH_ID}/0.jsonl": START})
        self.assertEqual(outside.read_bytes(), b"keep")

    def test_a_piece_the_list_and_the_bare_path_are_405(self):
        self.post(piece_path(index=0), START)
        for path in (piece_path(index=0), piece_path(index="0.jsonl"), f"/matches/{MATCH_ID}/", "/matches/", "/matches", f"/matches/{MATCH_ID}/0/0"):
            with self.subTest(path):
                response = self.delete(path)
                self.assertEqual((response.status, response.headers["allow"]), (405, "GET, HEAD, POST, DELETE"))
        self.assertEqual(list(self.stored()), [f"{MATCH_ID}/0.jsonl"])

    def test_a_folder_that_is_a_link_is_not_followed(self):
        outside = self.store.parent / "elsewhere"
        outside.mkdir()
        (outside / "0.jsonl").write_bytes(b"keep")
        try:
            (self.store / MATCH_ID).symlink_to(outside, target_is_directory=True)
        except (OSError, NotImplementedError):
            self.skipTest("no symbolic links here")
        self.assertEqual(self.delete(f"/matches/{MATCH_ID}").status, 404)
        self.assertEqual((outside / "0.jsonl").read_bytes(), b"keep")


class AuthOverTheNewPathsTest(MatchStoreTestCase):
    basic_auth = ("tester", "s3cret")

    def test_every_new_path_and_method_needs_the_credentials(self):
        self.post(piece_path(), START, basic(*self.basic_auth))
        for path, method in (("/matches/", "GET"), ("/matches/", "HEAD"), (piece_path(index="0.jsonl"), "GET"),
                             (piece_path(index="0.jsonl"), "HEAD"), (piece_path(index="9.jsonl"), "GET"), (f"/matches/{MATCH_ID}", "DELETE"),
                             (piece_path(index=0), "DELETE"), ("/matches/x", "DELETE"), ("/matches", "GET")):
            with self.subTest(path=path, method=method):
                response = self.request(path, method=method)
                self.assertEqual(response.status, 401)
                self.assertTrue(response.headers["www-authenticate"].startswith("Basic realm="))
        self.assertEqual(list(self.stored()), [f"{MATCH_ID}/0.jsonl"])

    def test_with_the_credentials_the_answers_are_the_real_ones(self):
        headers = basic(*self.basic_auth)
        self.post(piece_path(), START, headers)
        self.assertEqual(json.loads(self.request("/matches/", headers=headers).body), {"matches": [{"id": MATCH_ID, "pieces": 1}]})
        self.assertEqual(self.request(piece_path(index="0.jsonl"), headers=headers).body, START)
        self.assertEqual(self.request(f"/matches/{MATCH_ID}", method="DELETE", headers=headers).status, 200)


def make_old_store(folder: Path, now: float) -> dict[str, int]:
    """Matches of every age in `folder`; returns the bytes of the pieces of each."""
    def piece(match: str, index: int, size: int, age_days: float):
        path = folder / match / f"{index}.jsonl"
        path.parent.mkdir(exist_ok=True)
        path.write_bytes(b"x" * size)
        os.utime(path, (now - age_days * DAY, now - age_days * DAY))

    piece("a" * 32, 0, 10, 89)
    piece("b" * 32, 0, 20, 91)
    piece("b" * 32, 1, 5, 120)
    piece("c" * 32, 0, 7, 200)
    piece("c" * 32, 1, 3, 1)
    return {"a": 10, "b": 25, "c": 10}


class SweepTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._temp.cleanup)
        self.store = Path(self._temp.name)
        self.now = START_TIME
        self.sizes = make_old_store(self.store, self.now)

    def names(self) -> list[str]:
        return sorted(path.name for path in self.store.iterdir())

    def test_a_match_is_kept_until_its_newest_piece_is_90_days_old(self):
        freed = serve.sweep_matches(self.store, self.now, 90 * DAY)
        self.assertEqual(self.names(), ["a" * 32, "c" * 32])
        self.assertEqual(freed, self.sizes["b"])
        self.assertEqual(sorted(path.name for path in (self.store / ("c" * 32)).iterdir()), ["0.jsonl", "1.jsonl"])

    def test_the_default_retention_is_90_days(self):
        self.assertEqual(serve.MATCH_RETENTION_SECONDS, 90 * DAY)
        serve.sweep_matches(self.store, self.now)
        self.assertEqual(self.names(), ["a" * 32, "c" * 32])

    def test_a_later_sweep_takes_the_rest(self):
        serve.sweep_matches(self.store, self.now + 2 * DAY, 90 * DAY)
        self.assertEqual(self.names(), ["c" * 32])
        self.assertEqual(serve.sweep_matches(self.store, self.now + 100 * DAY, 90 * DAY), self.sizes["c"])
        self.assertEqual(self.names(), [])

    def test_old_temp_files_and_empty_folders_go_young_temp_files_and_strangers_stay(self):
        old, young = self.now - 2 * 3600, self.now - 600
        (self.store / ("d" * 32)).mkdir()
        (self.store / ("e" * 32)).mkdir()
        (self.store / ("e" * 32) / ".tmp-old").write_bytes(b"half")
        (self.store / ("a" * 32) / ".tmp-old").write_bytes(b"half")
        (self.store / ("a" * 32) / ".tmp-young").write_bytes(b"half")
        (self.store / ".tmp-probe-old").write_bytes(b"")
        (self.store / ".tmp-probe-young").write_bytes(b"")
        (self.store / "notes").mkdir()
        (self.store / "readme.txt").write_bytes(b"x")
        (self.store / ("f" * 32)).mkdir()
        (self.store / ("f" * 32) / "readme.txt").write_bytes(b"x")
        for path, when in (("e" * 32 + "/.tmp-old", old), ("a" * 32 + "/.tmp-old", old), (".tmp-probe-old", old),
                           ("a" * 32 + "/.tmp-young", young), (".tmp-probe-young", young)):
            os.utime(self.store / path, (when, when))
        serve.sweep_matches(self.store, self.now, 90 * DAY)
        self.assertEqual(self.names(), [".tmp-probe-young", "a" * 32, "c" * 32, "f" * 32, "notes", "readme.txt"])
        self.assertEqual(sorted(path.name for path in (self.store / ("a" * 32)).iterdir()), [".tmp-young", "0.jsonl"])

    def test_a_missing_store_is_left_to_the_caller(self):
        with self.assertRaises(OSError):
            serve.sweep_matches(self.store / "nothing", self.now, 90 * DAY)

    def test_the_store_keeps_its_counter_right(self):
        store = serve.MatchStore(self.store, None, 30)
        # on disk: 10 + 25 + 10 = 45 bytes, over the cap of 30, so nothing new fits
        self.assertEqual(store.put("1" * 32, 0, b"x")[0], 507)
        store.sweep(self.now)  # match b goes: 20 bytes are left
        self.assertEqual(store.put("1" * 32, 0, b"y" * 10)[0], 200)  # 30
        self.assertEqual(store.put("1" * 32, 1, b"y" * 10)[0], 507)
        self.assertIs(store.delete("c" * 32), True)  # 20
        self.assertEqual(store.put("1" * 32, 1, b"y" * 10)[0], 200)

    def test_the_sweep_runs_at_start(self):
        clock = FakeClock(self.now)
        server = serve.make_server(self.store, "127.0.0.1", 0, quiet=True, match_store=self.store, clock=clock)
        self.addCleanup(server.server_close)
        self.assertEqual(self.names(), ["a" * 32, "c" * 32])

    def test_the_sweep_runs_again_every_interval_and_stops_when_asked(self):
        class Stop:
            """An Event that lets two intervals pass, 100 seconds each."""

            def __init__(self):
                self.waits = []

            def wait(self, interval):
                self.waits.append(interval)
                clock.now += 100
                return len(self.waits) > 2

        class Store:
            def __init__(self):
                self.swept = []

            def sweep(self, now):
                self.swept.append(now)

        clock, stop, store = FakeClock(10.0), Stop(), Store()
        serve.sweep_loop(store, clock, stop, 3600)
        self.assertEqual((store.swept, stop.waits), ([110.0, 210.0], [3600, 3600, 3600]))

    def test_the_thread_stops_with_the_server(self):
        server = serve.make_server(self.store, "127.0.0.1", 0, quiet=True, match_store=self.store)
        self.assertTrue(server.sweeper.is_alive())
        self.assertTrue(server.sweeper.daemon)
        server.server_close()
        self.assertFalse(server.sweeper.is_alive())

    def test_there_is_no_thread_without_a_store(self):
        server = serve.make_server(self.store, "127.0.0.1", 0, quiet=True)
        self.addCleanup(server.server_close)
        self.assertIsNone(server.sweeper)


class ConfigurationTest(unittest.TestCase):
    def parse(self, **environ):
        return serve.parse_args([], environ=environ)

    def test_the_defaults_are_a_store_that_is_off_and_a_gigabyte(self):
        args = self.parse()
        self.assertEqual((args.match_store, args.match_versions, args.match_store_max_bytes), (None, None, 1024 << 20))

    def test_the_forwarded_for_switch_is_off_unless_asked(self):
        self.assertIs(self.parse().match_forwarded_for, False)
        self.assertIs(self.parse(MATCH_FORWARDED_FOR="1").match_forwarded_for, True)
        self.assertIs(self.parse(MATCH_FORWARDED_FOR="0").match_forwarded_for, False)

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
        line = self.start_line(MATCH_STORE="<temp>/matches", MATCH_LOG_VERSIONS="1-5", BASIC_AUTH_USER="u", BASIC_AUTH_PASSWORD="p")
        self.assertIn("match store: ", line)
        self.assertIn("matches", line)
        self.assertNotIn("WARNING", line)

    def test_a_store_without_a_version_range_warns_on_the_start_line(self):
        line = self.start_line(MATCH_STORE="<temp>/matches")
        self.assertIn("WARNING", line)
        self.assertIn("MATCH_LOG_VERSIONS", line)

    def test_a_store_without_basic_auth_warns_that_the_endpoints_are_open(self):
        line = self.start_line(MATCH_STORE="<temp>/matches", MATCH_LOG_VERSIONS="1-5")
        self.assertIn("WARNING: no Basic auth", line)

    def test_the_store_folder_is_created_at_start(self):
        with tempfile.TemporaryDirectory() as temp:
            serve.make_server(Path(temp), "127.0.0.1", 0, match_store=Path(temp) / "a" / "b").server_close()
            self.assertTrue((Path(temp) / "a" / "b").is_dir())


class PieceRulesTest(unittest.TestCase):
    def test_json_constants_that_are_not_json_are_refused(self):
        for constant in ("NaN", "Infinity", "-Infinity"):
            body = b'{"event": "end", "x": ' + constant.encode() + b"}\n"
            self.assertEqual(serve.check_piece(body, 7, None), "a line is not JSON", constant)

    def test_a_crlf_line_ending_is_accepted(self):
        self.assertIsNone(serve.check_piece(b'{"event": "end"}\r\n', 7, None))

    def test_check_piece_names_a_reason_and_none_when_fine(self):
        self.assertIsNone(serve.check_piece(START, 0, (5, 5)))
        self.assertIsNone(serve.check_piece(START, 0, (1, 5)))
        self.assertIsNone(serve.check_piece(b"", 4, None))
        self.assertIsNotNone(serve.check_piece(START, 0, None))
        self.assertIsNotNone(serve.check_piece(b"", 0, (5, 5)))
        self.assertIsNone(serve.check_piece(json.dumps({"event": "end"}).encode() + b"\n", 7, None))


if __name__ == "__main__":
    unittest.main()
