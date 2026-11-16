# Art Generation Panel

## Overview

The editor's **Generate** panel (`ArtGenWidget`, command `view.generate`, View → Generate Art): a style preset of the game and a subject become image variants, and a chosen one becomes a sprite of the game. The editor knows neither ComfyUI nor any model: the images come from the art generation service (`tools/artgen/`, its `CLAUDE.md`), a separate Python process that is a **client of the control API**. Editor only (`ArtGen` is in the editor folders of `src/CMakeLists.txt`).

| File | Purpose |
|------|---------|
| `ArtGenSession.{h,cpp}` | The editor's side of the contract, no ImGui: the service's last status, one run (request, progress, variants, end), the control methods. `Singleton`: it outlives the panel |
| `ArtGenAccept.{h,cpp}` | Accept, no ImGui: the variant copied into the game's assets, the TextureLibrary entry, the reload of the sprites that use it, Use on a node; `AcceptForControl` is the session's acceptor in the editor |
| `ArtGenWidget.{h,cpp}` | The ImGui panel over the session: service line, preset, subject, count, seed, keep the variants, Generate / Cancel, progress, the grid of variants (Use this seed), Accept |

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
| `artgen_accept` | an agent, a test | `seed` (a variant of the current run), `sprite`, `useOnSelected?` | `{sprite, texturePath, png, width, height, reloadedSprites, usedOn}`; `artgen_no_variant`, `artgen_accept_failed` (a bad name, no TextureLibrary.xml, no selected node, a node without a SpriteRendererComponent) |
| `artgen_state` | an agent, a test | none | `{service: {connected, polls, state, reason, generator, presets}, run: {id, state, preset, subject, count, seed, image, step, steps, error, cancelRequested, variants: [{seed, path, provenance}]}, gameRoot}` |

- **One run at a time.** A request is `Queued` until a poll hands it over (`Running`), then `Done`, `Failed` or `Cancelled` by `artgen_done`. A report whose `id` is not the running one is answered `{"stale": true}` and changes nothing (a late message after a new request).
- **Cancel.** A queued run is cancelled at once (the service never saw it); a running one keeps running until the service answers `artgen_done` with `cancelled` after it saw `cancel` in a poll. The images already made stay.
- The panel and `artgen_generate` go through the same `ArtGenSession::Request`. A preset the service reported broken is refused there. `keepVariants` starts the new run with the variants of the last one (`keptVariants` of them), so more can be made without losing the ones on screen.

## Lifetime and threads

The control API cannot drop a handler and the panel is re-created at every switch of the run mode, so the methods are registered by the session (`RegisterControl`, once per `ControlService`, from the panel's `Initialize`) and capture the session, never the widget. Until the panel has been created once (an editor that started in Game mode) the methods do not exist and the service gets `unknown_method`; it waits. Main thread only, like every handler.

## The panel

- The service line says why nothing can happen: the control API off (`--control`), the service not connected (with the command to start it), or the generator not ready with its reason. A request can still be queued while the service is away.
- Thumbnails are loaded from the paths the service reported with `ResourceManager::Load` (an absolute path resolves as itself) and **evicted** (`ResourceManager::Evict`) when the run is replaced or the panel dies, so the generated textures do not stay in the cache until the editor closes.
- A click selects a variant (the frame and its seed); the dice button under it puts its seed into the form (random off) for the next Generate. Thumbnails are kept by path, so a run that kept the variants loads only the new ones.

## Accept

`ArtGenSession::Accept(seed, sprite, useOnSelected)` (the panel's button and `artgen_accept`) calls the acceptor the panel installed, `AcceptForControl`; the session test installs a fake, so the session needs no file system.

- **Files.** The PNG and its `.artgen.json` are copied to `<game assets>/artgen/<sprite>.png` and `.artgen.json` (the top `assets` layer, `FileSystem::GetMountedPath`), replacing an earlier accept of the name. A sprite name is 1 to 64 of `A-Z a-z 0-9 _ -` (`IsValidSpriteName`), so it is also a safe file name.
- **Library.** The entry `{sprite, assets/artgen/<sprite>.png, the whole image}` is added or replaced and `TextureLibrary::Save` writes the game's `TextureLibrary.xml`; when that fails (no such file) the entry in memory goes back to what it was. `Save` writes the whole file from the DOM, so its comments and layout are not kept.
- **Reload.** The path is evicted from the resource cache, then every `SpriteRendererComponent` of the active scene that names the sprite or the path is reloaded, so the new picture shows at once (a UI image of a canvas is not reloaded).
- **Use on selected.** The panel keeps the scene editor's primary selection in the session (`SceneSelectionChangedEvent`; held and found alive in the active scene before use). The sprite goes into its `SpriteRendererComponent::_spriteId` (a node without one is refused), and `SceneEditedEvent` tells the scene editor the scene is unsaved.

## Tests

`tests/BECore/ArtGenSessionTest.cpp` (editor regex of `tests/CMakeLists.txt`): a `ControlService` of its own, the methods called as the service calls them, a clock of the test, a fake acceptor. `ArtGenAcceptTest.cpp`: sprite names, the PNG size, the copy into a temporary assets folder. The service side is `tools/artgen/tests/test_artgen_bridge.py` against a fake editor that answers like the session.
