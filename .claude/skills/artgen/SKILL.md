---
name: artgen
description: Generates a game's art or sounds with the engine's art pipeline (ComfyUI through the editor's Generate panel) on a person's request, looks at the variants, and accepts the chosen one into the game (a sprite into TextureLibrary, a sound as a WAV clip) over the control API; starts ComfyUI, the editor (--editor) and the bridge when they are not running and closes what it started. Use when the user asks to generate or draw a sprite, an icon, a piece, a texture or a sound effect for the game, to make a set of them in one style, to replace a game's clip with a generated one, says "artgen", "сгенерируй спрайт", "нарисуй", "сделай звук", "сгенерируй набор".
---

# artgen

`/artgen <what to make>` ("a rook in the style of the game's pieces", "the sound of a capture", "all twelve pieces"). You make the art through the same path the person uses: the editor's Generate panel and its control API (`artgen_*`), never ComfyUI directly, so every result has its provenance and lands in the game the way an Accept by hand does. You never commit and never push.

Read first: `<engine>/src/Widgets/ArtGen/CLAUDE.md` ("The contract": every `artgen_*` method, its params and answer; "Accept", "Accept of a sound") and `<engine>/tools/artgen/CLAUDE.md` (setup, presets, the bridge).

Terms: `<engine>` is `engine` in a game (`.` in the engine's own repository); `<product>` is the field `Product` of `.claude/rules/build-project.md`, `<exe>` is `build/<preset>/bin/<product>` (`.exe` on Windows) with the Debug preset of that file; `<port>` is the control port (8765 is the default of `--control`); `<scratchpad>` the session's temporary folder. Commands run from the root of the repository; on Windows `python`, elsewhere `python3` (`.venv/bin/python` on macOS).

## Steps

1. **The generator.** `python <engine>/tools/artgen/comfyui_check.py --json`: exit 0 is ready (`kinds` says whether it makes `image`, `audio` or both); exit 2 is not running: `python <engine>/tools/artgen/artgen_setup.py start` (it waits until ComfyUI answers) and remember that you started it; exit 1 is a missing model: say which. **Never run `install`** (gigabytes, licences, the audio model needs the owner's Hugging Face token): give the command (`artgen_setup.py install`, `install --audio`) and stop. A sound asked from a generator without `audio` stops the same way.
2. **The editor.** Connect to `127.0.0.1:<port>` with `BagiControlClient` (`<engine>/tools/control/bagi_control_client.py`) and call `artgen_state`:
   - it answers: the person's editor is open. Work in it; the person sees your run in the Generate panel. Do not close it, do not save its scene, do not touch what the request does not name;
   - `unknown_method`: an editor in Game mode (the methods exist only after the panel was created once). Say so and stop: the person reopens it in Editor mode;
   - no connection: start your own. Build `<product>` through the `build` skill if `<exe>` is missing. Start `<exe> --editor --control --control-port=<a free port, not 8765>` in the background from the root (`--editor` opens the editor for this run; never edit `mode` of `config/ApplicationConfig.xml`) and connect until it answers (a Debug build takes several seconds). An `<exe>` that ignores `--editor` (an engine older than 0.1.12) answers `unknown_method`: rebuild it.
3. **The service.** `service.connected` false: start the bridge in the background, `python <engine>/tools/artgen/artgen_bridge.py --port <port> --exit-with-editor` (it ends with that editor), and poll `artgen_state` until `service.connected` and `service.state` is `ready`. A connected service is the panel's or the person's: never start a second bridge beside it.
4. **The preset.** `service.presets` lists the game's presets, then the engine's starters (`sprite`: one object cut out on a transparent background; `icon`; `sfx`: a short sound), each with its `kind`; a preset with an `error` is broken. Take the game's preset that fits the request (a game's style lives there: `<game root>/artgen/presets/<name>.json`, read it). When none fits, send a **draft** (the preset's content inline, `presetName` naming it) built on the closest one, and say what you changed. A preset is written to the game (`artgen_save_preset`) only when the person asks for it.
5. **Generate.** `artgen_generate {preset, subject, count, seed?}` (count 4 for a choice, 1 to remake a known seed; `artgen_busy` is a run already in work, the person's: wait for it or ask, never `artgen_cancel` a run you did not start). Poll `artgen_state` every second or two until `run.state` is `Done`, `Failed` or `Cancelled`; `run.error` is the reason of a failure. The variants are `run.variants: [{seed, path, provenance, kind}]` (files in `<game root>/artgen/out/`, git-ignored). An image takes seconds to tens of seconds on the owner's GPU, a sound about as long as an image; a run that has not moved for several minutes is reported, not waited for forever.
6. **Judge.**
   - **Images:** open every variant's PNG with `Read` and look: the object is the asked one and whole (not cut by the edge), one object, no text or watermark, no extra limbs or parts, the cut-out clean (no halo, no background left, nothing of the object cut away), and it matches the style of what the game already has (look at the accepted sprites in `assets/artgen/` and the game's atlas). Drop the broken ones; when none is good, generate again with other seeds or a changed prompt (at most three rounds, then show the best and say why it is not good enough).
   - **Sounds:** you cannot hear them. Read each variant's provenance (`seconds`, `sampleRate`, `channels`, the prompt) and drop only what is plainly wrong (a length far from the asked one). The choice is the person's: give them the seeds and say to listen in the Generate panel (View → Generate Art, Play beside each).
7. **Accept.** It writes the game's files (`assets/artgen/`, `config/TextureLibrary.xml`, a clip), so accept when the request asked for the result to be put into the game; otherwise show the pick and ask.
   - An image: `artgen_accept {seed, sprite}` (a sprite name is 1 to 64 of `A-Z a-z 0-9 _ -`; an existing name is replaced, so check the library first when the person did not name it). `useOnSelected: true` only when the person asked to put it on the selected node.
   - A sound: only the seed the person picked. `artgen_accept {seed, name}` writes `assets/artgen/<name>.wav`; `{seed, clip: "assets/.../x.wav"}` replaces a clip the game plays (find the clip paths in the game's sound docs or config first). `sound` overrides the processing (`maxSeconds`, `peakDb`, fades; the defaults are in "Accept of a sound"). The answer gives the length and the peak written: report them.
8. **Sets.** Many subjects in one style (the pieces of a set, the sounds of a board): one preset, the same `seed` for every subject, `count` 2 to 4, one run per subject; judge each against the ones already accepted, and remake a subject that does not match with other seeds. Accept the set together, so the game never shows half of an old set and half of a new one.
9. **Finish.** If you started the editor: `quit` over the control API, wait for the process (the bridge exits with it). If you started ComfyUI: `artgen_setup.py stop`, unless the person wants to go on generating. Leave the person's editor, bridge and ComfyUI running.
10. **Report**, in the person's language, short: what was generated (preset, subject, seeds), what you dropped and why, what was accepted where (paths, the sprite or clip name, for a sound its length and peak), what waits for the person (sounds to listen to, a pick between two), and `git status` of what changed in the game, uncommitted: committing is the person's word.

## The example script

A session in the background while you watch its output; change the request, keep the shape.

```python
"""python artgen-<slug>.py <port>: one run over the editor's control API; prints the variants as JSON."""

import json
import sys
import time

sys.path.insert(0, "engine/tools/control")  # "tools/control" in the engine's own repository
from bagi_control_client import BagiControlClient  # noqa: E402

client = BagiControlClient("127.0.0.1", int(sys.argv[1]))
client.connect()
for _ in range(120):
    service = client.call("artgen_state")["service"]
    if service["connected"] and service["state"] == "ready":
        break
    time.sleep(0.5)
else:
    sys.exit(f"the service is not ready: {service['state']} {service['reason']}")

client.call("artgen_generate", {"preset": "sprite", "subject": "chess rook", "count": 4})
deadline = time.monotonic() + 600
while time.monotonic() < deadline:
    run = client.call("artgen_state")["run"]
    if run["state"] in ("Done", "Failed", "Cancelled"):
        break
    time.sleep(1.0)
print(json.dumps({"state": run["state"], "error": run["error"], "variants": run["variants"]}, indent=1))
client.close()
```

## Forbidden

`artgen_setup.py install` or `artgen_setup {"action": "Install"}` without the owner's yes; reading, printing or writing the Hugging Face token; cancelling or replacing a run you did not start; accepting a sound nobody listened to; saving the scene or changing nodes beyond `useOnSelected`; editing workflows, the manifest or `ApplicationConfig.xml`; `git commit`, `git push`.
