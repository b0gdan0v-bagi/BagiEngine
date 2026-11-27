# The web build

You will build the demo for a browser, serve it from your own machine and open it at `http://127.0.0.1:8080/`. The engine is compiled to WebAssembly, so a game is a folder of static files that any web server can hand out. The first run takes about ten minutes, most of it downloading the compiler once.

You need the checkout of the repository and Python 3.10 or newer. Nothing else is installed on the machine: the compiler lands inside the checkout, and no administrator rights are needed.

## 1. What you get

The build writes a **page**: `index.html`, a script, a `.wasm` (the engine and the game), a `.data` file that holds the game's `config/` and `assets/`, and a `manifest.json` that lists them. There is one page per game, so you say which game you build.

The browser is not a desktop, and the page differs from the program of [Run the demo](run-the-demo.md) in a few ways:

- The game draws into a canvas of the page. It has no window of its own.
- It has no control port: a page cannot listen on one.
- It does not watch files. The `config/` and `assets/` are packed into the `.data` file when the page is built, so a file you changed on disk reaches the page with the next build.
- Its workers need two headers from the server (section 5), or the page does not start.
- Its saves live in the browser (section 7).

## 2. Set up the compiler, once

<!-- command -->
```bash
python3 CI/scripts/setup_emsdk.py
```

This installs the Emscripten SDK, the compiler for the web, into `external/emsdk`, which is a submodule of the repository. It is about 2 GB and took seven minutes on the machine this page was written on, mostly download. It writes nothing outside the checkout, and `git status` stays clean. A second run does nothing when the SDK is there; `python3 CI/scripts/setup_emsdk.py --check` only says whether it is.

## 3. Build

<!-- command -->
```bash
python3 tools/web/build_and_deploy.py --game BagiDemo --preset web-release
```

The script configures and builds the preset `web-release` for `BagiDemo`, then packs the result into `dist/`. It took about a minute and a half here. It builds and packs and does nothing else: nothing in the command leaves your machine.

Open `dist/`. It holds:

- `index.html`, `BagiDemo.js`, `BagiDemo.wasm` and `BagiDemo.data`: the page, the script that loads the rest, the code and the content. The three loaded files keep their names, because they load each other by name.
- A `.gz` copy of each of them (and a `.br` copy when the Python that ran the build has the `brotli` module). The server hands out the smaller copy when the browser accepts it.
- `manifest.json`, which says what is in the folder.
- `manifest.webmanifest` and `icons/`, which let a phone install the page as an app, and `third-party-notices.txt`, which the page links.

<!-- illustration: the shape of manifest.json of this run; the sizes and hashes are those of one build, cut with ... -->
```json
{
  "commits": { "engine": "...", "game": "..." },
  "files": {
    "BagiDemo.data": { "gzip": 1248723, "sha256": "...", "size": 2658021 },
    "BagiDemo.wasm": { "gzip": 1683989, "sha256": "...", "size": 4951538 },
    ...
  },
  "game": "BagiDemo",
  "threads": true
}
```

`game` says whose page it is and `threads` says whether it needs the headers of section 5. The server reads `threads`, and so can you when a page does not start.

## 4. Serve it

<!-- command -->
```bash
python3 tools/web/serve.py --root dist --host 127.0.0.1 --port 8080
```

`--host 127.0.0.1` makes the server answer this machine only, which is what you want for a page you are testing: without it the server listens on every address of the machine, and the other computers of your network can open it. The start line says what the server decided:

<!-- illustration: the start line of serve.py for this build; the port is the one you pass -->
```text
serve.py: dist on http://127.0.0.1:8080 (cross-origin isolation on: manifest.json says threads: true, a threaded build; access: open; match store: off)
```

Open `http://127.0.0.1:8080/` in a current desktop browser. The page shows a loading bar while the browser fetches about 3 MB (the compressed files), then the demo scene appears, the same as in [Run the demo](run-the-demo.md): the title, the sprites, the sparks and the panel with `Burst`, `Animate` and the `Emission` slider, which work with a click or a tap. A button at the top right makes the page full screen. Press `Ctrl+C` in the terminal to stop the server.

What the writer saw, in a headless Chromium on a Mac: the page started, `crossOriginIsolated` was true, the log said `Created with 4 threads`, and the scene was drawn. Not observed: Firefox and Safari, a phone, a sound (a headless browser plays none).

If the page stays blank, open the console of the browser (`F12`): a page that needs the headers and did not get them says so in the window itself (next section), and any other failure is a line in the console. A browser that shows an old page after you built again is holding the earlier files: the server sends `no-cache` and an `ETag`, so a plain reload takes the new ones.

## 5. Why the page needs two headers

The default build is **threaded**: the engine's workers are Web Workers that share the memory of the wasm. A browser lets a page share memory like that only when the page is *cross-origin isolated*, which it becomes when the server sends two headers with every answer:

- `Cross-Origin-Opener-Policy: same-origin`
- `Cross-Origin-Embedder-Policy: require-corp`

`serve.py` sends them for a threaded page by itself (the start line above says so). A server that does not send them, any other static server or a file opened from the disk, gets a page that does not start and says why:

<!-- illustration: the text of the page when it has the threads but not the headers (serve.py with --no-cross-origin-isolation) -->
```text
This page needs cross-origin isolation
This game runs its work on worker threads, which need the page to be cross-origin isolated: ...
```

The two headers have a price: they break a page that embeds another site without that site's consent, which this page does not. If you cannot send them, build the page **without threads**:

<!-- command -->
```bash
python3 tools/web/build_and_deploy.py --game BagiDemo --preset web-release --single-thread --dist build/dist-single
python3 tools/web/serve.py --root build/dist-single --host 127.0.0.1 --port 8080
```

`--single-thread` builds the preset `web-release-single-thread` (the engine runs its tasks on the one thread) and says `"threads": false` in the manifest, so `serve.py` sends no headers. Seen in the same headless Chromium: `crossOriginIsolated` was false, the log said `Created inline (no worker threads)`, and the scene was drawn. The price is that the work the workers would do now runs on one thread.

## 6. The editor in a page

A page can carry the editor too. It is a build of its own, because a page must never have it by accident:

<!-- command -->
```bash
python3 tools/web/build_and_deploy.py --game BagiDemo --preset web-editor
```

The preset `web-editor` is the Debug build with the editor, so it takes longer and its `.wasm` is much larger. The package is written to `build/web-editor-dist` (and not to `dist/`), and the page starts in the editor, as `--editor` does on the desktop. [The editor page](editor.md) says what is different in it.

An editor can change everything, so `serve.py` will not start an editor page for others to reach. It starts on a loopback address only, which `--host 127.0.0.1` is:

<!-- command -->
```bash
python3 tools/web/serve.py --root build/web-editor-dist
```

<!-- illustration: the answer of serve.py when the host is not a loopback address (the default is every address); the message is cut with ... -->
```text
serve.py: build/web-editor-dist holds an editor page, which needs access control: ... Without it the page is served to this machine only: --host 127.0.0.1 ...
```

With `--host 127.0.0.1` it starts and says `access: open`. The page is slow to start: the Debug `.wasm` is about 138 MB, and it took about half a minute here before the editor was drawn. It then asks you to confirm that nothing you change in it is kept.

## 7. Saves in the browser

A game that saves ([A save](save-game.md)) writes into a folder that the page keeps in the browser's IndexedDB, and it is loaded again at the next visit. They are the site's data: clearing the data of the site clears the saves, and two tabs of one address share them. The headless browser reported `persistent` for the state of the folder. The control API of a page, which is how a script plays a page, is a later page of this guide.

Putting a page on a server for other people is a separate step that the owner of the repository keeps, and this guide does not describe it.

## What you did

You installed a compiler into the checkout, built the demo for the web, looked at what the page is made of, served it on your own machine and saw why a threaded page needs two headers and how the single-thread build avoids them.

Next (planned): the control API from Python.
