# Art Generation Panel

## Overview

The editor's **Generate** panel (`ArtGenWidget`, command `view.generate`, View → Generate Art): a style preset of the game and a subject become image variants. The editor knows neither ComfyUI nor any model: the images come from the art generation service (`tools/artgen/`, its `CLAUDE.md`), a separate Python process that is a **client of the control API**. Editor only (`ArtGen` is in the editor folders of `src/CMakeLists.txt`).

| File | Purpose |
|------|---------|
| `ArtGenSession.{h,cpp}` | The editor's side of the contract, no ImGui: the service's last status, one run (request, progress, variants, end), the control methods. `Singleton`: it outlives the panel |
| `ArtGenWidget.{h,cpp}` | The ImGui panel over the session: service line, preset, subject, count, seed, Generate / Cancel, progress, the grid of variants |

## The contract

The service **pulls**: a request the panel makes before the service connects is not lost, and the polls are how the editor knows the service is alive (`kServiceTimeoutSeconds` without one: gone). Push events are not used (`ControlService::Publish` goes to every client and is lost when nobody listens). Every method answers at once; a run is never a deferred call (a deferred call holds back every later request).

| Method | Caller | Params | Answer |
|--------|--------|--------|--------|
| `artgen_poll` | service, about every 0.5 s | `state` (`ready` / `not_running` / `not_installed`), `reason`, `generator`, `presets: [{name, title, error?}]` | `gameRoot` (the real path of the game, where `artgen/presets/` is), `request: {id, preset, subject, count, seed\|null}` once, `cancel: id` while a cancel is asked |
| `artgen_progress` | service | `id, image, count, step, steps, seed` | `{stale}` |
| `artgen_image` | service, per image | `id, image, count, seed, path, provenance` (real paths of the PNG and its `.artgen.json`) | `{stale}` |
| `artgen_done` | service, once per run | `id, status` (`ok` / `failed` / `cancelled`), `error` | `{stale}` |
| `artgen_generate` | an agent, a test | `preset, subject, count?` (1..16, default 4), `seed?` | `{id, serviceConnected}`; `artgen_busy` while a run is in work, `invalid_params` |
| `artgen_cancel` | an agent, a test | none | `{}`; `artgen_no_run` |
| `artgen_state` | an agent, a test | none | `{service: {connected, polls, state, reason, generator, presets}, run: {id, state, preset, subject, count, seed, image, step, steps, error, cancelRequested, variants: [{seed, path, provenance}]}, gameRoot}` |

- **One run at a time.** A request is `Queued` until a poll hands it over (`Running`), then `Done`, `Failed` or `Cancelled` by `artgen_done`. A report whose `id` is not the running one is answered `{"stale": true}` and changes nothing (a late message after a new request).
- **Cancel.** A queued run is cancelled at once (the service never saw it); a running one keeps running until the service answers `artgen_done` with `cancelled` after it saw `cancel` in a poll. The images already made stay.
- The panel and `artgen_generate` go through the same `ArtGenSession::Request`. A preset the service reported broken is refused there.

## Lifetime and threads

The control API cannot drop a handler and the panel is re-created at every switch of the run mode, so the methods are registered by the session (`RegisterControl`, once per `ControlService`, from the panel's `Initialize`) and capture the session, never the widget. Until the panel has been created once (an editor that started in Game mode) the methods do not exist and the service gets `unknown_method`; it waits. Main thread only, like every handler.

## The panel

- The service line says why nothing can happen: the control API off (`--control`), the service not connected (with the command to start it), or the generator not ready with its reason. A request can still be queued while the service is away.
- Thumbnails are loaded from the paths the service reported with `ResourceManager::Load` (an absolute path resolves as itself) and **evicted** (`ResourceManager::Evict`) when the run is replaced or the panel dies, so the generated textures do not stay in the cache until the editor closes.
- A click selects a variant (the frame and its seed). Accept into the game, Use on selected, `artgen_accept` and regenerating one variant are the next slice of `editor-art-generate-panel`.

## Tests

`tests/BECore/ArtGenSessionTest.cpp` (editor regex of `tests/CMakeLists.txt`): a `ControlService` of its own, the methods called as the service calls them, a clock of the test. The service side is `tools/artgen/tests/test_artgen_bridge.py` against a fake editor that answers like the session.
