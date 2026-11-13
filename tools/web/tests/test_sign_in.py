"""Tests of the sign-in of tools/web/serve.py (ACCESS.md): providers, guests, sessions, the collect token and the stamp of a match.
A fake token endpoint stands in for Google; nothing leaves the machine. Run: python -m unittest discover tools/web/tests"""

import base64
import contextlib
import io
import json
import unittest
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

from test_serve import ServerTestCase  # first: it puts tools/web on the path
from web_fixtures import GAME

import serve

CLIENT_ID = "client-1.apps.googleusercontent.com"
SECRET = b"s" * 40
COLLECT = "c" * 40
MATCH_ID = "0123456789abcdef0123456789abcdef"
START = b'{"event":"start","version":5}\n'
NOW = 1_700_000_000.0


class FakeClock:
    def __init__(self):
        self.now = NOW

    def __call__(self) -> float:
        return self.now


def jwt(claims: dict) -> str:
    def part(value) -> str:
        return base64.urlsafe_b64encode(json.dumps(value).encode()).rstrip(b"=").decode()
    return f"{part({'alg': 'RS256'})}.{part(claims)}.c2lnbmF0dXJl"


class FakeGoogle:
    """The token endpoint: answers the next exchange with an ID token of `claims`, whose nonce is the one the start sent away unless a
    test sets it; records what was posted."""

    def __init__(self):
        self.email = "Tester@Example.com"
        self.overrides: dict = {}
        self.nonce = None
        self.posted = []
        self.clock = None

    def claims(self) -> dict:
        claims = {"iss": "https://accounts.google.com", "aud": CLIENT_ID, "exp": self.clock() + 600, "nonce": self.nonce,
                  "email": self.email, "email_verified": True, "sub": "123"}
        claims.update(self.overrides)
        return claims

    def exchange(self, fields: dict) -> dict | None:
        self.posted.append(fields)
        return {"id_token": jwt(self.claims())}


def cookie_of(response, name: str) -> str | None:
    """The value a response sets for cookie `name` (the headers of http.client keep only the last Set-Cookie, so the raw list is read)."""
    for header in response.set_cookies:
        key, _, rest = header.partition("=")
        if key == name:
            return rest.split(";")[0]
    return None


class SignInTestCase(ServerTestCase):
    guests = True
    public_url = "https://mahjong.example"
    store = True

    def server_options(self, root: Path) -> dict:
        self.clock = FakeClock()
        self.google = FakeGoogle()
        self.google.clock = self.clock
        provider = serve.OidcProvider("google", "Google", "https://accounts.google.com/o/oauth2/v2/auth", "https://token.invalid",
                                      ("https://accounts.google.com",), CLIENT_ID, "client-secret", exchange=self.google.exchange)
        self.sign_in = serve.SignIn([provider], self.public_url, SECRET, 30 * 24 * 3600, self.guests, self.clock)
        self.store_folder = root / "store"
        options = dict(clock=self.clock, sign_in=self.sign_in, collect_token=COLLECT)
        if self.store:
            options.update(match_store=self.store_folder, match_versions=(5, 5))
        return options

    def request(self, path, method="GET", headers=None, body=None):
        import http.client
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            connection.request(method, path, body=body, headers=headers or {})
            raw = connection.getresponse()
            set_cookies = raw.headers.get_all("Set-Cookie") or []
            response = type("R", (), {})()
            response.status = raw.status
            response.headers = {name.lower(): value for name, value in raw.getheaders()}
            response.set_cookies = set_cookies
            response.body = raw.read()
            return response
        finally:
            connection.close()

    def sign_in_as(self, email: str | None = None) -> str:
        """Runs the whole flow with the fake Google; returns the Cookie header of the session."""
        if email is not None:
            self.google.email = email
        start = self.request("/auth/google/start")
        location = urlsplit(start.headers["location"])
        query = {key: values[0] for key, values in parse_qs(location.query).items()}
        self.google.nonce = query["nonce"]
        state_cookie = cookie_of(start, serve.STATE_COOKIE)
        callback = self.request(f"/auth/google/callback?state={query['state']}&code=the-code",
                                headers={"Cookie": f"{serve.STATE_COOKIE}={state_cookie}"})
        self.assertEqual((callback.status, callback.headers["location"]), (303, "/"))
        return f"{serve.SESSION_COOKIE}={cookie_of(callback, serve.SESSION_COOKIE)}"

    def guest(self) -> str:
        response = self.request("/auth/guest")
        self.assertEqual(response.status, 303)
        return f"{serve.SESSION_COOKIE}={cookie_of(response, serve.SESSION_COOKIE)}"

    def post_piece(self, cookie: str, index: int = 0, body: bytes = START, match_id: str = MATCH_ID):
        return self.request(f"/matches/{match_id}/{index}", "POST", {"Cookie": cookie, "Content-Length": str(len(body))}, body)

    def collect(self, path: str = "/matches/", method: str = "GET"):
        return self.request(path, method, {"Authorization": f"Bearer {COLLECT}"})


class DoorTest(SignInTestCase):
    def test_without_a_session_the_page_goes_to_the_sign_in_and_a_file_is_401_without_a_basic_challenge(self):
        page = self.request("/")
        self.assertEqual((page.status, page.headers["location"]), (303, "/signin"))
        script = self.request(f"/{GAME}.js")
        self.assertEqual(script.status, 401)
        self.assertNotIn("www-authenticate", script.headers)

    def test_healthz_stays_open(self):
        self.assertEqual(self.request("/healthz").status, 200)

    def test_the_sign_in_page_offers_every_provider_and_the_guest_with_no_script(self):
        page = self.request("/signin")
        self.assertEqual(page.status, 200)
        text = page.body.decode()
        self.assertIn('href="/auth/google/start"', text)
        self.assertIn('href="/auth/guest"', text)
        self.assertNotIn("<script", text)
        self.assertIn("default-src 'none'", page.headers["content-security-policy"])
        self.assertEqual(page.headers["cache-control"], "no-store")

    def test_a_signed_in_visitor_gets_the_game(self):
        cookie = self.sign_in_as()
        self.assertEqual(self.request("/", headers={"Cookie": cookie}).status, 200)
        self.assertEqual(self.request(f"/{GAME}.js", headers={"Cookie": cookie}).status, 200)

    def test_the_sign_in_page_names_the_account_and_offers_sign_out(self):
        text = self.request("/signin", headers={"Cookie": self.sign_in_as("A<b>@example.com")}).body.decode()
        self.assertIn("a&lt;b&gt;@example.com", text)
        self.assertIn('href="/auth/signout"', text)

    def test_sign_out_clears_the_cookie(self):
        response = self.request("/auth/signout", headers={"Cookie": self.sign_in_as()})
        self.assertEqual((response.status, response.headers["location"]), (303, "/signin"))
        self.assertEqual(cookie_of(response, serve.SESSION_COOKIE), "")
        self.assertTrue(any("Max-Age=0" in header for header in response.set_cookies))

    def test_the_cookies_are_httponly_lax_and_secure_over_https(self):
        start = self.request("/auth/google/start")
        state = next(header for header in start.set_cookies if header.startswith(serve.STATE_COOKIE))
        for flag in ("HttpOnly", "SameSite=Lax", "Secure", "Path=/auth/google/"):
            self.assertIn(flag, state)
        session = next(header for header in self.request("/auth/guest").set_cookies if header.startswith(serve.SESSION_COOKIE))
        for flag in ("HttpOnly", "SameSite=Lax", "Secure", "Path=/", f"Max-Age={30 * 24 * 3600}"):
            self.assertIn(flag, session)

    def test_an_unknown_provider_is_404(self):
        self.assertEqual(self.request("/auth/apple/start").status, 404)
        self.assertEqual(self.request("/auth/google/other").status, 404)


class FlowTest(SignInTestCase):
    def test_the_start_sends_the_client_and_the_exact_redirect_to_google(self):
        location = urlsplit(self.request("/auth/google/start").headers["location"])
        self.assertEqual(f"{location.scheme}://{location.netloc}{location.path}", "https://accounts.google.com/o/oauth2/v2/auth")
        query = {key: values[0] for key, values in parse_qs(location.query).items()}
        self.assertEqual(query["client_id"], CLIENT_ID)
        self.assertEqual(query["redirect_uri"], "https://mahjong.example/auth/google/callback")
        self.assertEqual((query["response_type"], query["scope"]), ("code", "openid email"))
        self.assertTrue(query["state"] and query["nonce"])

    def test_the_code_is_exchanged_with_the_client_secret_and_the_same_redirect(self):
        self.sign_in_as()
        posted = self.google.posted[-1]
        self.assertEqual((posted["code"], posted["client_secret"], posted["grant_type"]), ("the-code", "client-secret", "authorization_code"))
        self.assertEqual(posted["redirect_uri"], "https://mahjong.example/auth/google/callback")

    def failed(self, response) -> bool:
        return response.status == 303 and response.headers["location"] == "/signin?failed=1" and cookie_of(response, serve.SESSION_COOKIE) is None

    def callback(self, state_override: str | None = None, with_cookie: bool = True):
        start = self.request("/auth/google/start")
        query = {key: values[0] for key, values in parse_qs(urlsplit(start.headers["location"]).query).items()}
        self.google.nonce = self.google.nonce or query["nonce"]
        headers = {"Cookie": f"{serve.STATE_COOKIE}={cookie_of(start, serve.STATE_COOKIE)}"} if with_cookie else {}
        return self.request(f"/auth/google/callback?state={state_override or query['state']}&code=x", headers=headers)

    def test_a_wrong_or_missing_state_fails_before_the_exchange(self):
        self.assertTrue(self.failed(self.callback(state_override="forged")))
        self.assertTrue(self.failed(self.callback(with_cookie=False)))
        self.assertEqual(self.google.posted, [])

    def test_a_state_older_than_ten_minutes_fails(self):
        start = self.request("/auth/google/start")
        query = {key: values[0] for key, values in parse_qs(urlsplit(start.headers["location"]).query).items()}
        self.clock.now += serve.STATE_SECONDS + 1
        response = self.request(f"/auth/google/callback?state={query['state']}&code=x",
                                headers={"Cookie": f"{serve.STATE_COOKIE}={cookie_of(start, serve.STATE_COOKIE)}"})
        self.assertTrue(self.failed(response))

    def test_every_bad_claim_fails(self):
        for override in ({"aud": "another-client"}, {"iss": "https://evil.example"}, {"exp": NOW - 1}, {"email_verified": False},
                         {"email_verified": "true"}, {"nonce": "other"}, {"email": None}, {"aud": [CLIENT_ID, "x"], "azp": "x"}):
            with self.subTest(override=override):
                self.google.overrides = override
                self.google.nonce = None
                self.assertTrue(self.failed(self.callback()))

    def test_an_error_from_the_provider_fails(self):
        start = self.request("/auth/google/start")
        query = {key: values[0] for key, values in parse_qs(urlsplit(start.headers["location"]).query).items()}
        response = self.request(f"/auth/google/callback?state={query['state']}&error=access_denied",
                                headers={"Cookie": f"{serve.STATE_COOKIE}={cookie_of(start, serve.STATE_COOKIE)}"})
        self.assertTrue(self.failed(response))

    def test_a_tampered_an_expired_or_a_foreign_session_is_refused(self):
        cookie = self.sign_in_as()
        name, _, value = cookie.partition("=")
        data, _, signature = value.partition(".")
        forged = base64.urlsafe_b64encode(b'{"p":"google","e":"boss@example.com","x":9999999999}').rstrip(b"=").decode()
        self.assertEqual(self.request(f"/{GAME}.js", headers={"Cookie": f"{name}={forged}.{signature}"}).status, 401)
        other = serve.SignIn([], "https://x.example", b"o" * 40, 3600, True, self.clock)
        self.assertEqual(self.request(f"/{GAME}.js", headers={"Cookie": f"{name}={other.sign({'p': 'guest', 'x': NOW + 99})}"}).status,
                         401)
        self.clock.now += 31 * 24 * 3600
        self.assertEqual(self.request(f"/{GAME}.js", headers={"Cookie": cookie}).status, 401)

    def test_the_sign_in_routes_are_rate_limited_per_address(self):
        for _ in range(serve.AUTH_RATE_LIMIT):
            self.assertEqual(self.request("/auth/google/start").status, 303)
        refused = self.request("/auth/guest")
        self.assertEqual(refused.status, 429)
        self.assertIn("retry-after", refused.headers)


class GuestTest(SignInTestCase):
    def test_a_guest_plays_and_posts_a_match_with_no_stamp(self):
        cookie = self.guest()
        self.assertEqual(self.request("/", headers={"Cookie": cookie}).status, 200)
        self.assertEqual(self.post_piece(cookie).status, 200)
        self.assertEqual(json.loads(self.collect().body), {"matches": [{"id": MATCH_ID, "pieces": 1}]})
        self.assertFalse((self.store_folder / MATCH_ID / serve.MATCH_TESTER_NAME).exists())


class NoGuestsTest(SignInTestCase):
    guests = False

    def test_the_guest_route_is_gone_and_a_guest_cookie_does_not_open(self):
        self.assertEqual(self.request("/auth/guest").status, 404)
        self.assertNotIn("/auth/guest", self.request("/signin").body.decode())
        guest = self.sign_in.sign({"p": "guest", "x": NOW + 99})
        self.assertEqual(self.request(f"/{GAME}.js", headers={"Cookie": f"{serve.SESSION_COOKIE}={guest}"}).status, 401)


class MatchStampTest(SignInTestCase):
    def test_a_signed_in_match_is_stamped_and_listed_with_the_account(self):
        cookie = self.sign_in_as()
        self.assertEqual(self.post_piece(cookie).status, 200)
        listed = json.loads(self.collect().body)
        self.assertEqual(listed, {"matches": [{"id": MATCH_ID, "pieces": 1,
                                               "tester": {"provider": "google", "email": "tester@example.com"}}]})

    def test_only_the_same_account_adds_to_a_stamped_match(self):
        first = self.sign_in_as("one@example.com")
        self.assertEqual(self.post_piece(first).status, 200)
        second = self.sign_in_as("two@example.com")
        self.assertEqual(self.post_piece(second, 1, b"").status, 403)
        self.assertEqual(self.post_piece(self.guest(), 1, b"").status, 403)
        self.assertEqual(self.post_piece(first, 1, b"").status, 200)

    def test_a_session_never_lists_reads_or_deletes_matches_the_token_does(self):
        cookie = self.sign_in_as()
        self.post_piece(cookie)
        for path, method in (("/matches/", "GET"), (f"/matches/{MATCH_ID}/0.jsonl", "GET"), (f"/matches/{MATCH_ID}", "DELETE")):
            with self.subTest(path=path, method=method):
                self.assertEqual(self.request(path, method, {"Cookie": cookie}).status, 401)
        self.assertEqual(self.collect(f"/matches/{MATCH_ID}/0.jsonl").body, START)
        self.assertEqual(self.request("/matches/", headers={"Authorization": "Bearer wrong"}).status, 401)
        self.assertEqual(self.collect(f"/matches/{MATCH_ID}", "DELETE").status, 200)
        self.assertFalse((self.store_folder / MATCH_ID).exists())

    def test_a_stamp_left_alone_by_a_failed_write_is_swept(self):
        folder = self.store_folder / MATCH_ID
        folder.mkdir(parents=True)
        (folder / serve.MATCH_TESTER_NAME).write_text("{}", encoding="utf-8")
        serve.sweep_matches(self.store_folder, NOW)
        self.assertFalse(folder.exists())


class LogTest(SignInTestCase):
    def test_the_access_log_holds_no_code_state_or_email(self):
        self.server.RequestHandlerClass.quiet = False
        captured = io.StringIO()
        with contextlib.redirect_stderr(captured):
            cookie = self.sign_in_as("secret.person@example.com")
            self.post_piece(cookie)
            self.request("/", headers={"Cookie": cookie})
        log = captured.getvalue()
        self.assertIn("GET / ", log)
        for secret in ("the-code", "state=", "secret.person", "/auth/", MATCH_ID):
            self.assertNotIn(secret, log)


class FakeProviderTest(SignInTestCase):
    def server_options(self, root: Path) -> dict:
        self.clock = FakeClock()
        self.sign_in = serve.SignIn([serve.FakeProvider("Local@Test")], "http://127.0.0.1:1", SECRET, 3600, True, self.clock)
        return dict(clock=self.clock, sign_in=self.sign_in)

    def test_the_fake_signs_in_without_any_network(self):
        start = self.request("/auth/fake/start")
        self.assertEqual(urlsplit(start.headers["location"]).path, "/auth/fake/callback")
        callback = self.request(start.headers["location"],
                                headers={"Cookie": f"{serve.STATE_COOKIE}={cookie_of(start, serve.STATE_COOKIE)}"})
        self.assertEqual(callback.status, 303)
        session = self.sign_in.unsign(cookie_of(callback, serve.SESSION_COOKIE))
        self.assertEqual((session["p"], session["e"]), ("fake", "local@test"))


class SettingsTest(unittest.TestCase):
    def parse(self, environ: dict, *argv: str):
        return serve.parse_args(["--root", "dist", *argv], environ)

    def refused(self, environ: dict, *argv: str) -> str:
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr), self.assertRaises(SystemExit):
            self.parse(environ, *argv)
        return stderr.getvalue()

    GOOD = {"GOOGLE_CLIENT_ID": CLIENT_ID, "GOOGLE_CLIENT_SECRET": "x", "PUBLIC_URL": "https://mahjong.example/",
            "SESSION_SECRET": "k" * 32}

    def test_no_provider_leaves_the_server_as_it_was(self):
        self.assertIsNone(self.parse({}).sign_in)

    def test_google_with_its_settings(self):
        settings = self.parse(self.GOOD).sign_in
        self.assertEqual(settings["providers"], [("google", CLIENT_ID, "x")])
        self.assertEqual(settings["public_url"], "https://mahjong.example")
        self.assertTrue(settings["guests"])
        self.assertEqual(settings["session_seconds"], 30 * 24 * 3600)
        self.assertFalse(self.parse({**self.GOOD, "SIGNIN_GUESTS": "0"}).sign_in["guests"])
        sign_in = serve.make_sign_in(settings)
        self.assertEqual(list(sign_in.providers), ["google"])

    def test_the_refusals(self):
        cases = [
            ({"GOOGLE_CLIENT_ID": CLIENT_ID}, "set together"),
            ({**self.GOOD, "BASIC_AUTH_USER": "u", "BASIC_AUTH_PASSWORD": "p"}, "exclude each other"),
            ({**self.GOOD, "SESSION_SECRET": "short"}, "SESSION_SECRET"),
            ({**self.GOOD, "PUBLIC_URL": "http://mahjong.example"}, "PUBLIC_URL"),
            ({**self.GOOD, "PUBLIC_URL": "https://mahjong.example/game"}, "PUBLIC_URL"),
            ({**self.GOOD, "SESSION_DAYS": "0"}, "SESSION_DAYS"),
            ({**self.GOOD, "MATCH_COLLECT_TOKEN": "short"}, "MATCH_COLLECT_TOKEN"),
            ({"SIGNIN_FAKE_EMAIL": "a@b"}, "local tests only"),
        ]
        for environ, words in cases:
            with self.subTest(words=words):
                self.assertIn(words, self.refused(environ))

    def test_the_fake_only_on_a_loopback_host_and_never_on_railway(self):
        settings = self.parse({"SIGNIN_FAKE_EMAIL": "a@b"}, "--host", "127.0.0.1", "--port", "9000").sign_in
        self.assertEqual((settings["fake_email"], settings["public_url"]), ("a@b", "http://127.0.0.1:9000"))
        self.assertIn("local tests only", self.refused({"SIGNIN_FAKE_EMAIL": "a@b", "RAILWAY_ENVIRONMENT": "production"},
                                                       "--host", "127.0.0.1"))

    def test_a_local_public_url_may_be_http(self):
        self.assertEqual(self.parse({**self.GOOD, "PUBLIC_URL": "http://localhost:8080"}).sign_in["public_url"], "http://localhost:8080")


class PrivacyTest(SignInTestCase):
    def server_options(self, root: Path) -> dict:
        options = super().server_options(root)
        options["privacy_contact"] = "owner<x>@example.com"
        return options

    def test_the_privacy_page_is_open_and_names_the_contact_and_what_is_kept(self):
        page = self.request("/privacy")
        self.assertEqual(page.status, 200)
        text = page.body.decode()
        self.assertIn("owner&lt;x&gt;@example.com", text)
        self.assertIn("90", text)
        self.assertIn("30 days", text)
        self.assertNotIn("<script", text)
        self.assertIn("default-src 'none'", page.headers["content-security-policy"])

    def test_the_sign_in_page_links_it(self):
        self.assertIn('href="/privacy"', self.request("/signin").body.decode())
        self.assertIn('href="/terms"', self.request("/signin").body.decode())

    def test_the_terms_page_is_open_and_links_the_privacy_page(self):
        page = self.request("/terms")
        self.assertEqual(page.status, 200)
        self.assertIn('href="/privacy"', page.body.decode())
        self.assertIn("owner&lt;x&gt;@example.com", page.body.decode())


class PrivacyBehindBasicTest(ServerTestCase):
    basic_auth = ("user", "password")

    def test_the_privacy_page_needs_no_password(self):
        self.assertEqual(self.request("/privacy").status, 200)
        self.assertEqual(self.request("/terms").status, 200)


class ClaimsTest(unittest.TestCase):
    def test_decode_refuses_what_is_not_a_jwt(self):
        for token in (None, "", "a.b", "a.!!!.c", f"a.{base64.urlsafe_b64encode(b'[1]').decode()}.c"):
            self.assertIsNone(serve.decode_id_token(token))

    def test_the_email_is_lower_case(self):
        provider = serve.OidcProvider("google", "Google", "a", "b", ("iss",), "id", "secret", exchange=lambda fields: None)
        claims = {"iss": "iss", "aud": "id", "exp": NOW + 1, "nonce": "n", "email": "MiXed@Example.com", "email_verified": True}
        self.assertEqual(provider.check_claims(claims, "n", NOW), "mixed@example.com")

    def test_a_failed_exchange_is_no_identity(self):
        provider = serve.OidcProvider("google", "Google", "a", "b", ("iss",), "id", "secret", exchange=lambda fields: None)
        self.assertIsNone(provider.identity({"code": "c"}, "n", "r", NOW))


if __name__ == "__main__":
    unittest.main()
