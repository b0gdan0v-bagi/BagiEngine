# The web files of the engine

The files the browser build links or packs in (`cmake/BagiWeb.cmake`); they name no game. `config/` is the web overlay of the content (`BAGI_WEB_ROOT`). The tools that package and serve the page are in `tools/web/CLAUDE.md`.

| File | What it is |
|------|------------|
| `shell.html` | The page around the wasm (`--shell-file`): canvas, WebGL test, loading bar, error messages, fullscreen, `window.bagiWebPost` (the page of the match upload) |
| `saves.js` | The persistent save folder (`--pre-js`, with `-lidbfs.js`): `/bagi/saves` kept in the site's IndexedDB. The C++ side is `src/Modules/BECore/Save/SaveStorage.*` (`Save/CLAUDE.md`, "In the browser") |

## `saves.js`

**Why IDBFS:** it is Emscripten's own IndexedDB-backed file system, so `SaveGameService` keeps using `std::filesystem` unchanged. It copies the whole folder between memory and IndexedDB by timestamp (`FS.syncfs`), which suits a handful of small saves, not a large data set. The database is named after the mount point.

**The rules of the script:**

- **Load before the start.** A `Module.preRun` function makes the folder, mounts IDBFS, holds the start with `addRunDependency` and runs `FS.syncfs(true)`; `main()` runs after the answer, so the game's first `Initialize` sees the stored files. Any failure (no `indexedDB`, refused or blocked, a read error, no `FS`/`IDBFS` in the build) or **no answer within `kLoadTimeoutMs`** releases the start with the plain in-memory folder: the state is `"memory"`, the saves are gone at reload, the start is never stopped. After the time-out a late answer is ignored and the mount is never written again (a late load would overwrite what the game wrote meanwhile; the files a late load already restored stay). Nothing runs in a pthread worker or without a `window`.
- **`Module.bagiSaves`** is the contract with C++ and with the smoke: `state` (`"persistent"` or `"memory"`; `"memory"` until the load is answered), `markDirty()`, `flush(callback)` (`callback(null)` or `callback(message)`). A module linked without `saves.js` has no such object and reads as not remembered.
- **Writes:** `markDirty()` is a debounce (`kFlushDelayMs`) with an upper bound (`kFlushMaxWaitMs`) from the first call of a burst; one `FS.syncfs(false)` at a time, a change during it makes exactly one more after it; a failed sync (quota, closed database) is reported once with `console.warn` (until one succeeds) and tried again by the next `markDirty()` or a flush. `pagehide` and `visibilitychange` to `hidden` flush at once, best effort (the browser may cut the transaction, then the last write is lost). A sync that never answers blocks the later ones.
- **Main thread only:** the build has no `PROXY_TO_PTHREAD`, so the game and the mount are on the main thread.

**Limits:** the quota is the browser's (per site); clearing the site data clears the saves; a private window or a browser that refuses IndexedDB (Firefox private mode) gives `"memory"`; Safari may evict the data of a site not visited for a week; two tabs of one origin share the database, the last flush wins and a tab sees the other's writes only after a reload.

`tools/web/tests/test_saves_js.py` runs the script under node with a fake `FS` and timers; the real reload round trip is check (8) of the browser smoke.
