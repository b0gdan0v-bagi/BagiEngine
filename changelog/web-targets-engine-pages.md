# Web deploy: an editor page delivered to a page of a site (engine.bagiz.dev/demo) and the skill deploy-editor

- Version: 0.30.2
- Entry: web-targets-engine-pages
- Date: 2026-11-27
- Bump: patch

## Added
- A second kind of deploy target in `web/targets.json`: a page of a site (`{"game", "preset", "site", "page"}`) next to a Railway service. `tools/web/build_and_deploy.py --target <name> --deploy` builds and packages as before, then uploads the package as a plain tar (`PUT https://<site>/api/v1/builds/<page>`, sha256 checked by the site) instead of `railway up`; the token comes from `ENGINE_SITE_UPLOAD_TOKEN` in the environment or the project's `.env` (`.env.example`) and never reaches any output.
- `tools/web/site_upload.py`: the tar, the sha256 and the streamed upload, with the site's refusal named.
- The skill `deploy-editor` (shared with the games): on the owner's word, a dry run, then the deploy of a site-page target in the background, reported in Russian with the site's answer and the URL.

## Changed
- The engine's target `engine-editor` (a Railway service that no longer exists) is replaced by `engine-demo`: the BagiDemo editor page on `engine.bagiz.dev/demo`. A game's own Railway targets work as before.
