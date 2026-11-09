# Web Tools Guidelines

## Overview

Python tools for the browser build (entry `web-build` of the todo store; the CMake side is `cmake/BagiWeb.cmake` and the presets `web-release` / `web-debug`, `.claude/rules/cmake-rules.md`; the page is `web/shell.html`). They are separate processes with the standard library only (Python 3.10+; `brotli` is used when it can be imported). Nothing here names a concrete game apart from `BagiMatch3` as the default. None of it has run against a real Emscripten build yet: the tests build a fake build output.

| File | Purpose |
|------|---------|
| `package.py` | `python tools/web/package.py <build-dir> <out-dir> [--game NAME]`: the output of a web build (`<game>.html`, `.js`, `.wasm`, `.data`) into a folder to serve. The page becomes `index.html`, the other files keep their names (the page and the script load them by name), every `.html`, `.js`, `.wasm`, `.data` gets `name.gz` (always) and `name.br` (when `import brotli` works), and `manifest.json` has `{game, files: {name: {size, sha256, gzip, brotli}}}`. A `.br` left by an earlier run is deleted when this run makes none. Several pages in the build folder need `--game` |
| `serve.py` | The static server the Railway service runs. **One file that imports nothing of this repository** (the Docker image copies only it; a test checks that it imports only the standard library). `python serve.py --root dist [--host H] [--port P] [--cross-origin-isolation] [--quiet]`; `PORT` from the environment (default 8080), `COOP_COEP=1` is the same as the switch |
| `build_and_deploy.py` | The one command: `cmake --preset web-release`, `cmake --build --preset web-release --target <game>`, `package.py`, and with `--deploy` the staging and `railway up --service <name> --detach`. `--dry-run` prints the commands and runs nothing. Exit 2 with a message when `EMSDK` is not set (also with `--dry-run`) or, with `--deploy`, `railway` is not on PATH |
| `Dockerfile`, `railway.json` | The image (`python:3.12-slim`, `COPY serve.py` and `COPY dist/`, `CMD ["python", "serve.py", "--root", "dist"]`, user `nobody`) and the Railway settings (builder Dockerfile, health check `/healthz`) |
| `tests/` | `unittest` classes (`python -m unittest discover tools/web/tests` from the engine root; pytest also runs them where it is installed): `test_package.py`, `test_serve.py`, `test_deploy.py`, `test_cmake_web.py` (+ `check_bagi_web.cmake`, run under `cmake -P`; skipped without cmake), `test_shell.py`, and `web_fixtures.py` (a fake build output made in a temp folder; no binary file is committed) |

## The headers of `serve.py`, and why

| Rule | Why |
|------|-----|
| `Content-Type`: `application/wasm`, `application/octet-stream` for `.data`, `text/javascript`, `text/html; charset=utf-8`, `application/json` | The browser compiles a wasm only with `application/wasm` (streaming compile). The table is in the file and not in `mimetypes`: on Windows that reads the registry and has answered `text/plain` for `.js`. The type is that of the file, also when a compressed copy is sent |
| `name.br`, then `name.gz`, by `Accept-Encoding` (q-values read, `q=0` refuses, `*` allows both); `Content-Encoding`, `Vary: Accept-Encoding`, the `Content-Length` of the encoded body | The wasm and the content package are the large downloads. `name.gz` / `name.br` are never served as files of their own (404). A file with no variant gets no `Vary` |
| `index.html` and `manifest.json` (in the root): `Cache-Control: no-cache` plus `ETag` | A new deploy must show at once. The names of the other files are **not hashed** (`BagiMatch3.wasm` in every build), so a long cache or `immutable` would keep an old wasm beside a new page |
| everything else: `public, max-age=3600` plus `ETag`; `If-None-Match` (a list, `*`, weak tags) gives 304 with no body | An hour of reuse, then a cheap revalidation. The tag is `mtime-size-encoding`, so each encoding has its own and a changed file has a new one |
| `GET /healthz` -> 200 `ok` (`no-store`) | The health check of `railway.json` |
| `..`, `%2e%2e`, `%2f`, backslash, NUL, a link out of the folder -> 404 | The path is cut into segments before the file system is touched, and the resolved path must stay below the root |
| `HEAD` works, any other method is 405 with `Allow: GET, HEAD` | |
| `--cross-origin-isolation` (or `COOP_COEP=1`): `Cross-Origin-Opener-Policy: same-origin`, `Cross-Origin-Embedder-Policy: require-corp` on every answer, 304 and errors included | Off by default. **The single-thread build (`ENGINE_SINGLE_THREAD`, the only web build there is) needs neither:** no SharedArrayBuffer, no pthreads. The headers are for the day the owner decides on a threaded build, and they break a page that embeds another origin without CORP; the switch makes that decision cost nothing now |

## Deploying

`build_and_deploy.py` does it all from the root of a project that has the web presets (the engine, or a game that carries it as `engine/`; `--source-dir` names another): needs `EMSDK` (the Emscripten SDK, `emsdk_env` done), and for `--deploy` the Railway CLI, logged in (`railway login`) or with `RAILWAY_TOKEN` in the environment; the script reads and writes no secret. Output defaults: the build in `<source>/build/ClangWeb` (the web build writes to `build/<Compiler>Web`, the compiler of Emscripten is Clang), the package in `<source>/dist` (`dist/` is in the `.gitignore` of the engine; a game adds it to its own), the staging in `<source>/build/web-deploy`. `railway up` runs in the staging folder, which holds `Dockerfile`, `railway.json`, `serve.py` and a fresh copy of `dist/`, because the Dockerfile copies those and nothing else.

By hand: build with `cmake --preset web-release` and `cmake --build --preset web-release --target BagiMatch3`, `python tools/web/package.py build/ClangWeb dist`, then open the result locally with `python tools/web/serve.py --root dist` (http://localhost:8080). Create the service once in Railway: a project, an empty service (`railway add --service bagi-web`, or the dashboard), the domain under the service's Settings -> Networking; then `railway up --service bagi-web --detach` from a folder that holds the four files above, or let the script do it. Nothing is deployed by the tests or by the loop (the Railway service, the domain and the CI job are slice 3 of the entry).

**The `.data` package and relinks.** The content (`config/`, `assets/` of the engine, the game and the web overlay) is packed into `<game>.data` at **link time**. `be_web_configure_target` lists every file of those folders in `LINK_DEPENDS` (globbed with `CONFIGURE_DEPENDS`), so `cmake --build` relinks after an edit, an added or a deleted file; the link is the slow step (wasm-opt), which is the price of editing content.

## Tests

`python -m unittest discover tools/web/tests` from the engine root (in a game checkout: from `engine/`). `test_serve.py` starts the server on port 0 of 127.0.0.1 in a thread (`serve_forever(poll_interval=0.01)`, so a test does not wait half a second at its end) and one test starts `serve.py` as a program with `PORT=0`. `test_deploy.py` hands a fake runner to `build_and_deploy.run`; neither `railway`, `docker`, `cmake` nor an emsdk is needed. The real `brotli` module is tested only when installed; the other tests use a stand-in whose `.br` is a marker plus the input. If `docker` is at hand, `docker build` of the staged folder is a one-time manual check (not a test).
