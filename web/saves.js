/*
 * The save folder of a BagiEngine game in the browser: /bagi/saves kept in the site's IndexedDB (IDBFS), so a save survives a reload.
 *
 * Linked with --pre-js and -lidbfs.js (cmake/BagiWeb.cmake), so this text runs inside the Emscripten module and FS, IDBFS,
 * addRunDependency and Module are in reach. The C++ side is src/Modules/BECore/Save/SaveStorage.cpp; the contract, the limits and
 * the reasons are in web/CLAUDE.md.
 *
 * - Before main(): the folder is made and IDBFS mounted on it, the stored files are loaded (FS.syncfs(true)), and the start waits for
 *   that (a run dependency), so the game's first look at the folder already sees them.
 * - Any failure (no IndexedDB, refused or blocked, a sync error, no answer within kLoadTimeoutMs) leaves the plain in-memory folder and
 *   never stops the start. The state is then "memory" and the saves are gone at the next reload. After the time-out a late answer
 *   changes nothing and the mount is never written again: a late load would overwrite what the game wrote meanwhile.
 * - Module.bagiSaves is the contract with the game: state ("persistent" or "memory"), markDirty() (the folder changed: write it to
 *   IndexedDB soon) and flush(callback) (write it now). Writes are batched: a burst of markDirty() is one FS.syncfs(false), never two
 *   at once; a write during a sync makes one more after it; a failed sync is reported once with console.warn and tried again by the
 *   next markDirty(). The tab being hidden or closed flushes at once (best effort: the browser may cut the transaction).
 *
 * Checked by tools/web/tests/test_saves_js.py (node, fake FS and timers). It names no game.
 */
(function () {
    "use strict";

    // The folder of the page's virtual file system. Keep equal to kWebSaveDirectory in src/Modules/BECore/Save/SaveStorage.h
    // (test_saves_js.py checks it)
    var kSaveDirectory = "/bagi/saves";
    // The longest the start waits for IndexedDB to hand over the stored files
    var kLoadTimeoutMs = 5000;
    // A write is sent to IndexedDB this long after the last markDirty() ...
    var kFlushDelayMs = 1500;
    // ... but never later than this after the first one of a burst
    var kFlushMaxWaitMs = 10000;
    var kRunDependencyId = "bagi-saves";

    var kStatePersistent = "persistent";
    var kStateMemory = "memory";

    if (typeof window === "undefined" || typeof Module === "undefined") {
        return;  // a worker or a shell without a page
    }
    if (typeof ENVIRONMENT_IS_PTHREAD !== "undefined" && ENVIRONMENT_IS_PTHREAD) {
        return;  // the module's script also runs in every thread worker
    }

    var state = kStateMemory;
    var settled = false;       // the first load is over (answered, failed or timed out)
    var syncing = false;       // an FS.syncfs(false) is in flight
    var dirty = false;         // the folder holds something IndexedDB has not seen
    var urgent = false;        // a flush was asked for while a sync was in flight
    var warned = false;        // a failed sync is reported once until one succeeds
    var debounceTimer = null;
    var maxWaitTimer = null;
    var waiters = [];          // flush(callback) callers whose sync has not started yet

    function warn(text) {
        if (typeof console !== "undefined" && console.warn) {
            console.warn("[saves] " + text);
        }
    }

    function describe(error) {
        return error && error.message ? error.message : String(error);
    }

    function clearTimers() {
        if (debounceTimer !== null) {
            clearTimeout(debounceTimer);
            debounceTimer = null;
        }
        if (maxWaitTimer !== null) {
            clearTimeout(maxWaitTimer);
            maxWaitTimer = null;
        }
    }

    // A burst of markDirty() is one sync: the debounce timer moves with each call, the max-wait timer does not
    function arm() {
        if (state !== kStatePersistent || !dirty || syncing) {
            return;
        }
        if (debounceTimer !== null) {
            clearTimeout(debounceTimer);
        }
        debounceTimer = setTimeout(startSync, kFlushDelayMs);
        if (maxWaitTimer === null) {
            maxWaitTimer = setTimeout(startSync, kFlushMaxWaitMs);
        }
    }

    function startSync() {
        clearTimers();
        if (syncing || state !== kStatePersistent) {
            return;
        }
        syncing = true;
        urgent = false;
        dirty = false;
        var batch = waiters;
        waiters = [];
        var finished = false;

        function done(error) {
            if (finished) {
                return;
            }
            finished = true;
            syncing = false;
            if (error) {
                dirty = true;  // the next markDirty(), or a flush, tries again
                if (!warned) {
                    warned = true;
                    warn("the save folder could not be written to IndexedDB: " + describe(error));
                }
            } else {
                warned = false;
            }
            for (var i = 0; i < batch.length; ++i) {
                try {
                    batch[i](error ? describe(error) : null);
                } catch (callbackError) {
                    warn("a flush callback threw: " + describe(callbackError));
                }
            }
            if (waiters.length > 0 || (urgent && dirty)) {
                startSync();
            } else if (dirty && !error) {
                arm();  // written to during the sync: one more after it
            }
        }

        try {
            FS.syncfs(false, done);
        } catch (error) {
            done(error);
        }
    }

    function markDirty() {
        if (state !== kStatePersistent) {
            return;
        }
        dirty = true;
        arm();
    }

    function flush(callback) {
        if (state !== kStatePersistent) {
            if (typeof callback === "function") {
                setTimeout(function () { callback("not persistent"); }, 0);
            }
            return;
        }
        if (typeof callback === "function") {
            waiters.push(callback);
        }
        if (syncing) {
            urgent = true;
            dirty = true;
        } else {
            dirty = true;
            startSync();
        }
    }

    // The tab goes away or into the background: a timer may never fire there
    function flushIfDirty() {
        if (state !== kStatePersistent || !dirty) {
            return;
        }
        if (syncing) {
            urgent = true;
        } else {
            startSync();
        }
    }

    Module.bagiSaves = {
        get state() {
            return state;
        },
        markDirty: markDirty,
        flush: flush
    };

    function listenForLeaving() {
        try {
            window.addEventListener("pagehide", flushIfDirty);
            if (typeof document !== "undefined") {
                document.addEventListener("visibilitychange", function () {
                    if (document.visibilityState === "hidden") {
                        flushIfDirty();
                    }
                });
            }
        } catch (error) {
            warn("the save folder will not be flushed when the tab closes: " + describe(error));
        }
    }

    function mountAndLoad() {
        var timeoutTimer = null;

        // Ends the wait for the load once: the start goes on, with the folder persistent or plain memory
        function settle(persistent, reason) {
            if (settled) {
                return;
            }
            settled = true;
            if (timeoutTimer !== null) {
                clearTimeout(timeoutTimer);
                timeoutTimer = null;
            }
            if (persistent) {
                state = kStatePersistent;
                listenForLeaving();
            } else {
                state = kStateMemory;
                warn("the saves are kept in memory only and are gone at reload: " + reason);
            }
            try {
                removeRunDependency(kRunDependencyId);
            } catch (error) {
                warn("could not release the start: " + describe(error));
            }
        }

        try {
            if (typeof FS === "undefined" || typeof IDBFS === "undefined") {
                throw new Error("the build has no IDBFS");
            }
            FS.mkdirTree(kSaveDirectory);
            if (typeof indexedDB === "undefined" || !indexedDB) {
                // Nothing to wait for: the plain folder is made and the game starts
                warn("the saves are kept in memory only and are gone at reload: this browser has no IndexedDB");
                settled = true;
                return;
            }
            addRunDependency(kRunDependencyId);
        } catch (error) {
            settled = true;
            warn("the save folder was not mounted: " + describe(error));
            return;
        }

        try {
            FS.mount(IDBFS, {}, kSaveDirectory);
            timeoutTimer = setTimeout(function () {
                timeoutTimer = null;
                settle(false, "IndexedDB did not answer in time");
            }, kLoadTimeoutMs);
            FS.syncfs(true, function (error) {
                if (settled) {
                    return;  // timed out: a late answer is ignored
                }
                if (error) {
                    settle(false, "IndexedDB could not be read: " + describe(error));
                } else {
                    settle(true);
                }
            });
        } catch (error) {
            settle(false, "the mount failed: " + describe(error));
        }
    }

    // Module.preRun is an array, a function, or absent
    if (typeof Module.preRun === "function") {
        Module.preRun = [Module.preRun];
    } else if (!Module.preRun) {
        Module.preRun = [];
    }
    Module.preRun.push(mountAndLoad);
})();
