# Deploying a web build

`build_and_deploy.py` builds a game for the web, packages it and uploads it to a Railway service or to a page of a site ("A site page"). Two ways to start it: by hand in a working copy, and by a named **target** (`--target`), which builds in a checkout of its own at `origin/<base>` and is what an agent of the studio page starts. Nothing in the tests or the loop deploys anything.

## A hand run

It works from the root of a project that has the web presets (the engine, or a game that carries it as `engine/`; `--source-dir` names another). It needs the Emscripten SDK (`python CI/scripts/setup_emsdk.py` installs it into `external/emsdk` once per checkout, about 2 GB; the presets find it), and for `--deploy` the Railway CLI logged in (`railway login`, `railway link`), or `RAILWAY_TOKEN`; the script writes no secret and reads the variables of a service only for an editor page ("The editor page"). Defaults: the page in `<source>/build/<preset>/bin` (a single-thread preset's own: its libraries lack `-pthread`), the package in `<source>/dist` (in the engine's `.gitignore`; a game adds it to its own), the staging in `<source>/build/web-deploy`. The staging is emptied first (only inside `<source>/build`; another `--stage-dir` must be empty or absent, else exit 2) and then holds only `Dockerfile`, `serve.py` and a fresh copy of `dist/`; `railway up` gets it with `--path-as-root`, so it never uploads a linked parent such as the engine root.

**The gate:** `--deploy` stops before the build unless HEAD of the source has a green full test run and the tree is HEAD (`tools/alltests/CLAUDE.md`); `--skip-alltests` deploys anyway and says so, `--dry-run` only reports it. A hand run never starts the tests: it refuses and says how to start them.

By hand without the script: `cmake --preset web-release`, `cmake --build --preset web-release --target BagiStress`, `package.py build/web-release/bin dist`, `serve.py --root dist` (http://localhost:8080). Single-thread: the preset `web-release-single-thread`, `package.py build/web-release-single-thread/bin dist-single --single-thread`, `serve.py --root dist-single`. A threaded page over plain `http://` from a host other than `localhost` is never isolated: use `localhost` or https. The Railway service is made once (`railway add --service bagi-web`, a domain, `railway link`); its settings (Dockerfile, `/healthz`) are the game's Railway IaC partial (BagiMahjong: `web/.railway/railway.ts`). To close it: `railway variables --service <name> --set BASIC_AUTH_USER=<user> --set BASIC_AUTH_PASSWORD=<password>` and redeploy (the server reads them at start).

## Targets: `<project>/web/targets.json`

`--target NAME` reads the file next to the game's `web/app.json`, from the project that runs the script (not from the fetched checkout: the name is refused before any network use). The engine ships one target, `engine-demo` (the editor page of its demo scene, a page of the engine's site, `tools/demo/CLAUDE.md`); a game owns its own file. A target is of one of two kinds: a **Railway service** (`service` and `address`) or a **page of a site** (`site` and `page`); keys of both kinds in one target are refused.

```json
{"format": 1, "targets": {
  "mahjong-editor": {"game": "BagiMahjong", "preset": "web-editor", "service": "studio-mahjong", "address": "studio-mahjong.bagiz.dev"},
  "engine-demo": {"game": "BagiDemo", "preset": "web-editor", "site": "engine.bagiz.dev", "page": "demo"}}}
```

- **Required in both kinds:** `game` (`[A-Za-z0-9_]+`, the CMake target), `preset` (`web-[a-z0-9-]+`; a `-single-thread` suffix keeps its meaning). **A Railway service:** `service` (`[a-z0-9][a-z0-9-]*`, the Railway service), `address` (a lower-case host name: no scheme, port or path). **A site page:** `site` (a host name like `address`), `page` (`[a-z0-9][a-z0-9-]*`, a page the site lists). **Optional:** `base` (the branch the checkout is cut from, default `main`), `area` (the todo area passed to `alltests.py --area`, default `engine`). Target names are `[a-z0-9][a-z0-9-]*`.
- **Strict on purpose:** unknown keys, duplicate keys, a file over 64 KiB and any value that does not match its pattern are exit 2 before anything runs. Every value reaches a command as one argument, never through a shell, and none can start with `-`.
- **`--target` takes no free options.** `--game`, `--preset`, `--service`, `--single-thread`, `--build-dir`, `--app`, `--dist`, `--stage-dir`, `--skip-alltests` and `--alltests-results` together with it are exit 2: the target is the whole description of a deploy, so whoever starts it cannot add arguments, and the gate cannot be skipped. Allowed: `--deploy`, `--dry-run`, `--source-dir`, `--status-file`, `--checkout-dir`. `--target` implies `--fetch`.
- A dry run prints `target <name>: <game>, <preset>, <service>, <address>` (a site page: `<game>, <preset>, site <site>, page <page>`) and the steps; a site page's steps end with the lines of "A site page" only with `--deploy` (`--target <name> --deploy --dry-run`): without it a dry run prints the git and build steps and no upload lines.

## The checkout (`--fetch`, implied by `--target`)

The build runs in a **detached git worktree of the project's repository** at `origin/<base>`, not in the owner's working copy, so the page and the server (`package.py`, `serve.py`, `Dockerfile`) of one commit travel together. Where: `~/.cache/bagi-web-deploy/<repository>/checkout` (`BAGI_WEB_DEPLOY_HOME` moves the parent; `--checkout-dir` names another folder). It is a worktree rather than a clone so that one fetch updates both and the `alltests` results file (keyed by the folder of the main checkout) is shared: a commit green by `/alltests` is green here.

- **The only git commands** (`deploy_checkout.py`, each printed before it runs): `fetch --quiet origin +refs/heads/<base>:refs/remotes/origin/<base>` in the project, `rev-parse`, `worktree add --detach` (a new folder) or `checkout --force --detach` and `clean -fdq` (an existing one), then in the checkout `submodule sync --recursive` and `submodule update --init --recursive --force`. **Never** a push, a pull, a merge, a reset, a branch switch, `clean -x` or `--remote`: the engine submodule stays at the commit the game records. The project's own branches, index and working tree are not touched; ignored files in the checkout (`build/`, the venvs, the SDK) survive `clean`, which makes the second run fast.
- A folder that is not a worktree of the same repository is refused and kept (exit 2). The checkout is the project of the run: `build/`, `dist/`, `web/app.json` and the staging are its own, the tool files come from its engine (`<checkout>/engine` when it holds `tools/web/package.py`, else the checkout), and a missing SDK is installed there by `setup_emsdk.py` as the first build step.
- One run at a time: a lock file `deploy.lock` next to the checkout; a second run is **refused** at once (exit 2, the pid of the first), it does not wait.
- `--dry-run` prints the git commands with `<origin/<base>>` for the commit and runs none.

## The gate in checkout mode

With `--deploy` the commit of the checkout needs a green full test run. When the results file has none (or a non-green one), the run starts `<checkout engine>/tools/alltests/alltests.py --repo <checkout> --commit <sha> --area <area>` itself (stage `tests`, printed), asks the gate again and goes on only on green. Red stops before the build, exit 2, with `alltests is red for commit <sha> (failed: A, B)`; an `alltests.py` that exits 1 (an error, nothing recorded) is exit 2 with its code. A green record already there starts nothing. An uncommitted change in the checkout is refused without a run (a clean checkout is the contract, a dirty one a bug). A dry run says `alltests gate: asked after the fetch, on origin/<base>`.

`--fetch` by hand (without a target) with `--skip-alltests` keeps the old gate and starts no tests. **`--fetch` with `--alltests-results` is unsupported:** `alltests.py` always records into its own default results file, so a run that looks for another file ends with "no result"; leave the option out.

## The status file (`--status-file FILE`)

A JSON object rewritten whole, through a temp file and `os.replace`, at every change; a dry run and a run without the option write nothing. A file that cannot be written is reported once on stderr and does not stop the deploy. Nothing from the environment is written.

```json
{"format": 1, "target", "stages": ["fetch","tests","build","package","deploy"], "stage", "state": "running|done|failed",
 "commit", "engineCommit", "service", "address", "url", "answer", "failedTests", "error", "startedAt", "updatedAt"}
```

- `stages` lists what this run goes through: `fetch` only in checkout mode, `tests` there with `--deploy` (and no `--skip-alltests`), `deploy` only with `--deploy`; configure and build are stage `build`, `package.py` is `package`, the staging and `railway up` are `deploy`.
- `state: failed` carries the `stage` it stopped in and a one-line `error` (and `failedTests` after a red run). **`done` at the stage `deploy` means `railway up --detach` accepted the upload, not that the page is live;** `url` is `https://<address>`, set from the target. **For a site page** `done` means the site answered 200 and has switched the page; `service` is null, `address` is the site, `url` is `https://<site>/<page>/` and `answer` is the site's JSON object (only its documented keys `page`, `game`, `threads`, `editor`, `commits`, `uploadedAt`, `bytes`; null otherwise).

## A site page

A target with `site` and `page` sends the package to the site's `PUT https://<site>/api/v1/builds/<page>` (the contract is the site's `docs/builds.md`; `site_upload.py`). The gate, the fetch, the build and the packaging are those of every target; the Railway CLI, the staging and the access check of "The editor page" are not used, because the site owns the rule of the page. The skill `deploy-editor` runs this on the owner's word.

- **The upload:** the package folder becomes one uncompressed tar made with `tarfile` (files at its root; no `._*` or `.DS_Store`; a link or another kind of file is refused), in a temp folder that is removed in every case. The run prints the files, bytes and sha256, the `PUT` line and the site's answer. The body is streamed with a known `Content-Length`; the timeout counts each socket operation (`UPLOAD_TIMEOUT_SECONDS` of `site_upload.py`), not the whole upload. A redirect is not followed, so the token goes only to the site named by the target.
- **The token** is `ENGINE_SITE_UPLOAD_TOKEN` in the environment, else in `<project>/.env` (the project that runs the script, not the fetched checkout; `.env.example` has the block). A real `--deploy` looks for it before the fetch and the build, since a build takes minutes; a dry run reads none and names the variable and the file. It goes into one header and is in no output, status file, error text or command.
- **The answers:** 200 is the switch (`done`). Otherwise exit 2 with the status and the site's `detail`: 400 the body or its sha256 is wrong; 401 the token is missing or not the site's; 404 the site has no such page (a commit in the site's `service/pages.py`); 408 the body stopped for 60 s; 409 another upload runs, try again; 411 no `Content-Length`; 413 above `BUILD_UPLOAD_MAX_MB` of the site; 422 the package does not agree with its `manifest.json` (the detail names the file); 503 the site has no token set; 507 its volume cannot be written. A broken connection, a timeout and a refused TLS name their error type only.

## The editor page

`--preset web-editor` (by hand or in a target) builds the page with the editor and packages it into `<source>/build/web-editor-dist`; `dist/` is not touched. It is a debug build: tens of MB of `.wasm` (`CLAUDE.md`, "The package").

```bash
python tools/web/build_and_deploy.py --game BagiDemo --preset web-editor
python tools/web/serve.py --root build/web-editor-dist --host 127.0.0.1      # this machine only, http://localhost:8080
python tools/web/build_and_deploy.py --game BagiDemo --preset web-editor --deploy --service <editor service>
```

- **Set the access first** (`ACCESS.md`): Basic, or sign-in with `SIGNIN_ALLOW`, on a service of its own. A hand `--deploy` without `--service` is exit 2, also with `--dry-run`; a package that says `editor` under another preset name needs `--service` too.
- **The question:** after the build and before the staging the script runs `railway variable list --service <name> --json` where `railway up` will run (the same linked project and environment) and applies the rule of `serve.py` to the answer. No door, settings the server would not start with, a failed command or an answer that is not a JSON object of strings: exit 2, nothing staged or uploaded. A variable the CLI does not return (sealed) counts as unset. `--dry-run` says that the question would be asked and asks nothing; `--skip-alltests` skips the test gate only.
- The test gate is the one of the release page.

## Commits in the manifest

`package.py --commit NAME=SHA` (repeatable; `NAME` `[a-z][a-z0-9-]*`, `SHA` 7 to 64 lower-case hex digits) writes `commits: {name: sha}` into `manifest.json`; without the option the key is absent. The deploy script passes `engine` (HEAD of `<project>/engine` in a game, else of the project) and `game` (HEAD of the project; the same sha in the engine) whenever git answers for a folder that is the top of a repository, so a reader of the deployed `manifest.json` can say "outdated" by comparing them with `origin/<base>`. `serve.py` ignores the key (of the manifest it reads `threads` and `editor`).

## What stays the owner's

The upload token of the site and its list of pages (`BUILD_UPLOAD_TOKEN`, `service/pages.py`, `BUILD_UPLOAD_MAX_MB` live in the site's repository), the Railway services and domains, `railway login` or `RAILWAY_TOKEN` and the first `setup_emsdk.py` install on the machine that deploys, the game's own `web/targets.json`, and the studio hub's access to the page's `manifest.json` when the service is behind Basic or sign-in (`ACCESS.md`): it needs a credential for that read. One manual check once a targets file exists: `python tools/web/build_and_deploy.py --target <name> --deploy --dry-run`.
