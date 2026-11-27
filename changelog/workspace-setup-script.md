# One setup script for a new checkout: CI/setup_venv.* (venv, tools, todo sign-in); the PyQt launcher is gone

- Version: 0.31.3
- Entry: workspace-setup-script
- Date: 2026-11-27
- Bump: patch

## Changed
- `CI/setup_venv.sh` and `CI/setup_venv.ps1` (`CI/setup_venv.bat` now starts the `.ps1`) are the one setup step: they make or update `.venv` (with uv, or Python's own `venv` without it), install `requirements.txt` and `tools/control/requirements.txt`, check that the reflection generator finds Jinja2, and offer the todo sign-in (`--no-login` skips it); running them again is safe. `!start.command` and `!start.cmd` stay as double-click wrappers of them. Adapt: a game whose own `CI/setup_venv.*` calls the engine's keeps working; pass `--no-login` where no sign-in is wanted.
- The root `requirements.txt` no longer lists `PyQt6` and `python-dotenv`. Adapt: a game script that relied on them from the engine's venv installs them itself.
- The PyQt GUI launcher (`CI/launcher/`), `WORKSPACE_README.md`, `create_env.ps1` and `CI/meta_generator/setup_env.*` are deleted. Adapt: run `CI/setup_venv.*` (or `!start.*`) instead; a game's docs or wrappers that named the launcher point at the setup script.
