# Who may open a web build: open, Basic, or sign-in

`serve.py` has three doors, chosen by the variables of the service; the start line says which one is on (never a secret, a token or an email).

| Door | Variables | What a visitor sees |
|------|-----------|---------------------|
| Open | none | the game |
| Basic | `BASIC_AUTH_USER`, `BASIC_AUTH_PASSWORD` | the browser's password box (`CLAUDE.md`, "The headers of `serve.py`") |
| Sign-in | `PUBLIC_URL`, `SESSION_SECRET`, the client of one provider or more, optional `SIGNIN_GUESTS`, `SESSION_DAYS` | `/signin`: a button per provider, and "Play without signing in" unless `SIGNIN_GUESTS=0` |

Basic and sign-in exclude each other (the server does not start with both). Sign-in is for **identity, not a closed door**: any account of a configured provider comes in, and so does a guest. Its purpose is that a match a signed-in visitor sends carries who sent it.

## Sign-in

- **Providers.** `PROVIDERS` in `serve.py` is the table of the providers a deployment can turn on; one is on when both of its variables are set (Google: `GOOGLE_CLIENT_ID`, `GOOGLE_CLIENT_SECRET`). An OpenID Connect provider is one row (its authorization and token endpoints, its issuers); one that is not (plain OAuth with a profile call) is a `Provider` subclass with `authorize_url` and `identity`. The routes carry the provider id: `/auth/<id>/start`, `/auth/<id>/callback`.
- **The flow** is the authorization-code flow with the client secret on the server: `/auth/<id>/start` sends the visitor to the provider with a random `state` and `nonce` (kept in a signed cookie `bagi_signin`, path `/auth/<id>/`, 10 minutes); `/auth/<id>/callback` checks `state`, exchanges the code at the token endpoint, and checks the ID token's issuer, audience (and `azp` when there are several), expiry, `nonce` and `email_verified`. The signature of the ID token is not checked: it comes straight from the token endpoint over TLS, which OpenID Connect Core 3.1.3.7 (6) allows, and it keeps `serve.py` standard library only (no RSA). Why not the Google button script: a third-party script in a cross-origin isolated page needs CORP headers Google does not promise, and a popup fights `Cross-Origin-Opener-Policy: same-origin`; the code flow is two top-level redirects and works with the isolation headers on every answer.
- **The session** is a cookie `bagi_session` signed with `SESSION_SECRET` (HMAC-SHA256): `{"p": provider or "guest", "e": email, "x": expiry}`, `HttpOnly`, `SameSite=Lax`, `Secure` when `PUBLIC_URL` is https, `SESSION_DAYS` long (default 30). Nothing is stored on the server, so a new `SESSION_SECRET` ends every session at once (the way to throw everybody out). `/auth/signout` clears it; `/signin` with a session shows the account and the sign-out.
- **Without a session** a page (`/`, `*.html`) answers 303 to `/signin` and anything else 401 (no Basic challenge); `/healthz` stays open. The isolation headers are on every answer, the sign-in page included (it has no script and no resource of another origin; its `Content-Security-Policy` says so).
- **Limits:** 30 sign-in requests (start, callback, guest) an hour per client address (the address rule of the match store, `MATCH_FORWARDED_FOR`). The access log skips every `/auth/` line (code, state) and every `/matches/` line.

## Matches with sign-in

- **The stamp.** The first piece a signed-in visitor posts to a match writes `<store>/<id>/tester.json` = `{"provider", "email"}` before the piece; a later piece from another account (or a guest) is **403**, so nobody can add to someone else's match. A guest's match has no stamp. `GET /matches/` lists `"tester"` for a stamped match. The email comes from the provider's checked token, never from the page.
- **Collecting.** A session never lists, reads or deletes matches (every Google account can have one). `GET` / `DELETE /matches/...` need `Authorization: Bearer <MATCH_COLLECT_TOKEN>` (a service variable, 32 characters or more); without the token nobody can collect and the start line warns. The token also works beside Basic.

## Local tests

`SIGNIN_FAKE_EMAIL=me@example.com serve.py --root dist --host 127.0.0.1` adds a provider `fake` that signs in as that address without any network (`PUBLIC_URL` and `SESSION_SECRET` default to the local address and a random secret). It is refused unless `--host` is a loopback address and no `RAILWAY_*` marker is in the environment, so the Docker image (which listens on `0.0.0.0`) cannot turn it on.

## The owner's checklist (Google, once)

1. [Google Cloud console](https://console.cloud.google.com/): a project; **APIs & Services → OAuth consent screen**: External, app name, support email, scopes `openid` and `email` only.
2. **Publish the app** (consent screen → Publish app). In "Testing" only the listed test users (at most 100) can sign in; `openid email` are not sensitive scopes, so publishing needs no verification review.
3. **Credentials → Create credentials → OAuth client ID**, type Web application, **Authorized redirect URI** `https://<service domain>/auth/google/callback` exactly (no JavaScript origin is needed). Copy the client id and the secret.
4. `railway variables --service <name> --set GOOGLE_CLIENT_ID=... --set GOOGLE_CLIENT_SECRET=... --set PUBLIC_URL=https://<service domain> --set SESSION_SECRET=<python -c "import secrets; print(secrets.token_urlsafe(48))"> --set MATCH_COLLECT_TOKEN=<another one>`; remove `BASIC_AUTH_USER` and `BASIC_AUTH_PASSWORD` (`railway variables --service <name> --remove ...` or the dashboard); redeploy.
5. Check: the start line says `access: sign-in with google or as a guest (https://...)`; a private window lands on `/signin`; sign in, play, then `curl -H "Authorization: Bearer $MATCH_COLLECT_TOKEN" https://<domain>/matches/` lists the match with your email.

Another provider later: its client in its own console with the redirect `https://<domain>/auth/<id>/callback`, its two variables, redeploy.
