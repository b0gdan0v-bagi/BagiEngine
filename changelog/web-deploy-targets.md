# Web deploy: named targets, a build from a checkout of its own, and a status file an agent can follow

- Version: 0.25.4
- Entry: web-deploy-targets
- Date: 2026-11-25
- Bump: patch

## Added
- `tools/web/build_and_deploy.py --target <name>` reads the project's `web/targets.json` (game executable, preset, Railway service, public address) and refuses an unknown name; the build runs in a fetched, detached checkout of its own, the gate starts the full alltests run itself, progress is written to a status file stage by stage, and `manifest.json` records the commits (`tools/web/DEPLOY.md`). Adapt: a game that deploys a web page adds its `web/targets.json` and deploys by `--target`.
