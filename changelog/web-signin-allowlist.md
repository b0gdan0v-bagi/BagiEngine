# Web: the sign-in admits only the accounts of the service's own allowlist

- Version: 0.25.2
- Entry: web-signin-allowlist
- Date: 2026-11-25
- Bump: patch

## Added
- `tools/web/serve.py`: `SIGNIN_ALLOW` (a list of emails per service) is checked after the provider's ID token and again on every request, so an email taken off the list loses access at once; guests are off while the list is set; the session cookie stays host-only and signed with the service's own `SESSION_SECRET` (`tools/web/ACCESS.md`). Adapt: a deployed web service that should not admit every account sets `SIGNIN_ALLOW`.
