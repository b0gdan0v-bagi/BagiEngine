# Todo CLI loader: a new CLI version is smoke-checked before it runs, and a version can be pinned

- Version: 0.1.19
- Entry: engine-loader-cli-guard
- Date: 2026-11-16
- Bump: patch

## Changed
- `tools/todo/bagitodo_cli.py` is BagiTodo's loader at `1c3fab4`: a downloaded CLI version is imported once (`python -I`, 15 s) before it becomes current; a failure keeps the old one with one warning and a retry after a minute. `BAGITODO_CLI_PIN=<version>` runs that version from the cache without asking the service.
