# Control API: no log counters, no batch and no run-until for automated testers

- Version: 0.1.4
- Entry: control-qa-conveniences
- Date: 2026-11-16
- Bump: patch

## Added
- Control API: `status.log` warning and error counters, `log_mark` / `log_since` / `log_subscribe` with a pushed `log` event; `batch` runs a list of calls in one request; `run_until` steps frames until a JSON pointer condition holds.
- `GameSession` and the MCP server expose the same methods; the rule `control-qa-api.md` describes the QA flow.
