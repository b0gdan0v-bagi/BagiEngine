#pragma once

namespace BECore {

    class ControlService;
    class FileWatcher;

    /**
     * @brief Registers the file methods of the control API on @p watcher: a client that edited a file on disk looks at what changed and then applies it.
     *
     * - `files_rescan` answers `{"changes": [{"path", "kind": "changed" / "added" / "removed", "consumers": [name]}]}`, sorted by path: what differs on
     *   disk from the state at start or the last delivery (`FileWatcher::Rescan`). It delivers nothing and needs no frame.
     * - `files_apply {"paths"?: [string]}` delivers those changes through the watches' callbacks (all pending when `paths` is omitted) and answers
     *   `{"applied": [{"path", "consumers", "ok", "error"?}]}` after one frame, so what the consumers announced with an event is seen by the next request.
     *   A consumer that could not use the change (a config that does not parse) gives `ok: false` and its `error`.
     * - Both fail with `not_watching` while the watcher is not running (the `fileWatch` setting, the web); `paths` that is not a list of strings is `invalid_params`.
     *
     * @p watcher must outlive the registration (CoreManager registers its own). Registering twice replaces the handlers.
     */
    void RegisterFileWatchControl(ControlService& service, FileWatcher& watcher);

}  // namespace BECore
