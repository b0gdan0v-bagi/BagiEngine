# Art generation sets

## Overview

A **set** is one preset applied to a list of subjects: one **job** per subject, all with the same first seed so the style matches. The model (`ArtGenSet.{h,cpp}`) is plain data over the manifest (no ImGui, `ArtGenSession` or `ControlService`), so another reader of the manifest can use it. Editor only, like the rest of `ArtGen/`.

| File | Purpose |
|------|---------|
| `ArtGenSet.{h,cpp}` | `ArtGenSet`, `ArtGenSetJob`, `ArtGenSetVariant`, `ArtGenSetJobState`; the subject-line rules; the panel's row helpers; the manifest to and from JSON; the counting helpers, live and kept (`IsLive`, `AlternativesOf`, `KeepJob`) |
| `ArtGenSetStore.{h,cpp}` | The sets on disk, no session: `WriteManifest`, `LoadSets`, the manifest's relative paths |
| `ArtGenSetPanel.{h,cpp}` | The Set tab of the Generate panel, ImGui over the session |

## Names and subjects

- A set name and a job name are 1 to 64 of `A-Z a-z 0-9 _ -` (`IsValidSpriteName`): the set name is a folder, the job name is a folder, the file stem, and the sprite (or clip) name of Accept all. Job names are unique in a set; at most `kMaxSetJobs` jobs; `count` is 1 to `kArtGenSetMaxCount`.
- **Subject lines** (`ParseArtGenSubjects`; the API's one string): one job per line; the rules and the errors are its doc comment (`a cat: sitting` is one subject). Subjects that arrive as data (a control call) go through `CheckArtGenSubjects`.
- Names and timestamps (`DefaultArtGenSetName`, `created`, `accepted.date`) are UTC, so sorting the text sorts the time.

## Job states

`Pending`, `Running`, `Done`, `Failed` (with `error`), `Cancelled`. A set is **active** while a job is `Pending` or `Running` (`IsActive`); a failed or cancelled job never stops it. A `Done` job may have a **pick**, the seed of one of its variants.

**`more`** (`ArtGenSetMore {seed, count}`) marks a `Done` job queued again for extra variants (`Done` -> `Pending` -> `Running` -> `Done`; variants, pick and `accepted` are untouched). Every end of a job goes through `EndArtGenJob`: a more-run that fails or is cancelled (an editor closed in the middle too) leaves the job **`Done`** with `more` cleared and the reason in `error` as a note, because Retry of a failed row would drop its variants, pick and accepted. The new images are on `seed + i`, off the job's seeds, so no file is overwritten.

## The manifest

`<game root>/artgen/out/<set>/manifest.json`, format `kArtGenManifestFormat` (1). Only `WriteManifest` writes it (the session calls it at every change of a job); readers use `ArtGenSetFromJson`.

```
{"manifest": 1, "set", "created", "kind" ("image"|"audio"), "preset", "presetData" (a draft's content, else null),
 "count", "seed" (the first seed of every job), "processing" (an image set: null | {"settings", "frame"}),
 "reference" (null | {"mode", "image", "weight"?}), "atlas" (null | a group name),
 "jobs": [{"name", "subject", "state", "error", "variants": [{"seed", "file", "provenance", "kind"}],
           "pick": seed|null, "accepted": null | {"as", "date", "kept"}, "more": null | {"seed", "count" (1..16)}}]}
```

`processing` is the frame of an image set (`ArtGenSetProcessing`), null until the first Accept all ("Accept all"); a manifest without the key reads as null.

`atlas` is the atlas group an image set's sprites join: `artgen_enqueue`'s `atlas`, else the preset's, taken at enqueue and never changed (an additive key, format 1; a manifest without it reads as none; a sound set has none).

`reference` is the picture every job's run starts from or is drawn in the style of (`Reference/CLAUDE.md`), set at `artgen_enqueue` and never changed. It is an additive key like `processing`, so the format stays 1: an older manifest reads as none, an older editor ignores it. A malformed one is an error that names `reference` and its key.

A job with `accepted` and not `kept` is **live**: the game holds its pick and the other variants are alternatives to switch to. `kept` is written always; a manifest from before it existed has `accepted` without the key and reads as **kept**, so nothing becomes live by itself. `KeepJob` fixes a live job (`kept`, the date, the variants cut to the pick; the files stay on disk).

`file` and `provenance` are relative to the set folder with `/`, so the folder can be moved. Reading is checked (`Control/Json.h`): a wrong type, a bad or duplicate job name, an unknown job state, a `pick` that is not a seed of the job's variants, or a `manifest` number other than 1 is an error that says where (`jobs[2].variants[0]: 'seed' must be an integer`), never an abort; unknown keys are ignored, so a later format 1 writer can add keys; a job without `more` reads as none.

## On disk

`<game root>/artgen/out/<set>/<job>/<job>_<seed>.png|flac` (+ `.artgen.json`) and `<set>/manifest.json`; the wire's `outDir` of a job is `<set>/<job>`. The folder is git-ignored in a game and kept for review; nothing deletes a set.

- `WriteManifest` makes the folders and replaces the file atomically, so a crash leaves the old manifest or the new one. It refuses a bad set name, so no path leaves `artgen/out/`. `NewSetFolderFree` is false for a name whose folder exists, with or without a manifest: a new set never writes into the files of another, nor over a manifest that did not parse.
- `LoadSets` reads every `artgen/out/*/manifest.json`, oldest first (`created`, then name). **The folder's name is the set's name** (a moved folder still loads). A job that was `Pending` or `Running` comes back `Cancelled` (`kEditorClosedReason`) in memory; the file is not rewritten until the next change. A manifest that does not read or a folder with an invalid name is skipped with one warning and never touched. A file a variant names that is gone stays in the set: the Set tab shows a gap.
- `RelativeToSet` turns the real path the service reported into the manifest's form; `AbsoluteInSet` is the way back and refuses a path with `..`, so a hand-edited manifest cannot make Accept all read outside the set.

## The queue

The session (`ArtGenSessionSets.cpp`) runs the sets through the one run it already has; the service's poll gets the usual `request` with `outDir` and `name`.

- **One job at a time, first in first out.** `PumpSets` starts the first `Pending` job of the first set in the queue (`_setQueue`: sets in the order they were enqueued or retried; a retried set with nothing waiting goes behind the others) as an ordinary `_run` (`outDir` `<set>/<job>`, `name` the job's name, the set's preset, count, shared seed and `reference`, so Retry and More run with it too; a job with `more` its `more`'s count and seed). A set queued while the panel's slot holds a picture gets it only when the panel passes it. It runs from the poll (before the hand-over) and from the end of a run, never from `EnqueueSet`: **no poll, no job**. It also waits for a run, a setup action or a service that is silent or whose generator is not `ready`: the jobs stay `Pending`.
- `IsBusy()` is true while a run is in work or any set is active: `Request`, `RequestSetup` and `artgen_generate` refuse meanwhile, `EnqueueSet` does not.
- **A run ends a job** (`EndSetJob`): `Done`, `Failed` (the service's reason) or `Cancelled`, the manifest is written and the next job is pumped. A report of an older run is stale and touches no job. `artgen_image` appends the variant to the job (a seed seen again replaces) and writes the manifest.
- **Cancel.** A `Pending` job is cancelled at once; the running job through `Cancel` (`ArtGen/CLAUDE.md`, "The contract"); cancelling a set cancels its pending jobs first and then the running one, so the line moves on to the next set. **Disconnect** ends only the running job (`Cancelled`, "the service was stopped"); the others stay `Pending` and go when a service polls again.
- **Retry** puts `Failed` and `Cancelled` jobs back to `Pending` and forgets the earlier try: the new one writes the same files under the same seeds.
- **More variants** (`MoreVariants`, `artgen_more`) queues a set that is not active again: every `Done` row that is not kept and has room (`ArtGenMoreSkipReason`) goes `Pending` with `more`, nothing else of it moves, so the new images are alternatives to the pick. One fresh seed per call, free in every row that runs (`ArtGenSeedRangeFree`; a given one that is not is `invalid_params`), keeps the rows' look together. `kArtGenJobMaxVariants` (64) only gates this call: a row's extra images from `regenerate` can take it past 64. The first Accept all measures the frame over every variant, the new ones too; once it is fixed, a switch to a variant bigger than it fails in the acceptor.
- **The set's kind** comes from the service's catalog, empty until the first poll, so `EnqueueSet` only guesses `image`; `StartJob` reads it again and the kind of a reported variant overrides it (`RecordSetImage`).
- **Game root.** `SetGameRoot` reads the sets of the game only when the root changes (the panel calls it at every switch of the run mode, and a reload would turn a running job into `Cancelled`). The shared seed is drawn at `EnqueueSet` when none is given and written to the manifest. A taken default set name gets `-2`, `-3`, ...; a taken name the caller gave stays `invalid_params`.

## Control methods

The table is in `ArtGen/CLAUDE.md`; the handlers are in `ArtGenSessionSets.cpp`. All answer at once and keep nothing on a refusal. `artgen_enqueue` is refused only for what makes a bad set (`invalid_params`); it never says `artgen_busy`, a second set joins the line. `artgen_cancel` with `job` and no `set` takes the one set that has that job `Pending` or `Running`; two such sets is `invalid_params`. `artgen_more` is refused whole (a single run in work does not refuse it, the rows wait); the message of `invalid_params` names the rows left out and why. `artgen_pick` is allowed on any job that has the variant (the Accept all check, not the pick, wants `Done`); on a live row it also writes the game.

## Accept all

`ArtGenSession::AcceptSet` (`artgen_accept_set`) writes a **candidate of every job** into the game under the **job's name**, through the acceptors the single Accept uses (an image as the sprite, a sound as the clip `assets/artgen/<name>.wav` with default settings; no node, a set has one kind), so the two paths cannot drift. The candidate is the pick, else, for images only, the first variant, which becomes the pick. A sound still needs a pick on every row: only a person listens. It writes assets per row and `TextureLibrary.xml` once at the end (an image set), so it is checked as a whole first and a refusal touches nothing:

1. The set exists and is not active (`artgen_busy`); every row that is neither kept nor replaced has a candidate and is not failed or cancelled (`artgen_set_incomplete` names them: a job cannot be dropped, it is retried first).
2. Every row to write: a valid name, the candidate's file of the set's kind, its provenance when the manifest names one (the file system is asked, the manifest is not trusted), and an acceptor installed (`artgen_accept_failed`).
3. Then the rows in order, each through the set's frame (below). An image set runs inside the session's accept batch (`SetAcceptBatch`; `ArtGenLibraryBatch`): the library is saved once after the last file, and only then does each row get its pick and `accepted{as, date, kept=false}` and the manifest is written once, so the manifest never names a row whose library entry a failed save rolled back (that save's failure marks no row and names the files written). A sound set marks per row. A failure halfway returns the row, its position and the rows done, and stops: the marks of the rows done stay, and **calling it again is the repair** (an accept replaces the same name). A **kept** row is skipped (`{job, kept: true}` in the answer), and so is a **replaced** one (`{job, replacedBy: <set>}`, see below): skipped, not refused, so the other rows still go in. A row already live is written again from its current pick.

**An atlas group.** A set with `atlas` writes each row into the group without packing it and, with the packer installed (`SetAtlasPacker`; the editor's is `PackAtlasForControl`), packs the group once after the last row; the answer's `atlas` is that pack (null with no group, no row written or no packer, when each row packs by itself). The rows are marked before the pack, so a failed pack (`artgen_accept_failed`, the group is not packed) leaves them accepted and in the game; calling again or Pack in the Sprite Library repairs it, and a failed row packs nothing. A switch of a live row repacks at once.

**One frame for an image set.** The files of a set keep their relative size (a pawn smaller than a king), so one frame cuts and scales them all. At the **first Accept all** the session measures every variant of every Done job (the alternatives too), through its `FrameMeasurer`, with the settings of the set's preset (a draft: its own `accept` or the defaults; by name: the saved preset's), and stores `processing {settings, frame}` with the first row written (a refusal or a failed first row stores nothing). **From then on the frame never moves** (it is a set field: it survives Keep and a reload): Accept all again, a switch and Keep write through it, so every alternative has the same output size. Editing the preset afterwards does not reach the set; the way to another frame is a new set. One alternative far bigger than the rest sets the scale for everyone; a variant whose drawn box is outside the stored frame fails in the acceptor (only possible with `frameOf`).

- **Legacy.** A set with an accepted row and no `processing` (`IsLegacyAccepted`) keeps writing through the identity plan (full frames), so its rows never differ in scale.
- **`frameOf`.** `artgen_enqueue {frameOf: "<set>"}` copies that set's `processing` into the new set (an image set that has one, else `invalid_params`), so its Accept all measures nothing. A one-row replacement set would otherwise get a frame of its own and its object would come out as big as a king.

**Live rows and switching.** A written row is live: the game holds its pick, the job's other variants are alternatives. `PickVariant` (`artgen_pick`) on a live row **switches**: it checks the variant and its files, writes it through the acceptor of the set's kind under `accepted.as`, and only then sets `pick` and `accepted.date`, so a failed write (`artgen_accept_failed`, its message ends with `the row still names seed N`; a library failure after the PNG went in says the game's file was already replaced) leaves the manifest as it was; the pick the game already holds is not written again. `artgen_pick` adds the acceptor's answer as `switched`. On a row that is not accepted a pick is bookkeeping only; on a kept row it is `artgen_no_variant`. A switch does not check that the set is idle.

**Replaced rows.** The game has one file per name, so two sets can hold a live row under the same name (the skill queues a new one-subject set, with `frameOf`, of a row that does not fit). The set whose row of that name and kind has the later `accepted.date` owns the file (`FindNewerOwner`). An accept's date is strictly later than every other set's date of that name (`NewAcceptDate`: the second after it when the clock has not moved on), so two accepts within a second cannot tie; the tie rule (the set later in the list) is for manifests that already hold equal dates. The other row, live or kept, is **replaced by** that set: a switch is `artgen_accept_failed`, Keep of it `invalid_params`, both naming the set (either would overwrite the newer candidate or record a seed the game does not hold). Keep all skips such rows and lists them in `replaced: [{job, by}]`.

## Keep

`KeepSet(set, job?)` (`artgen_keep`) makes a live row final: the **Keeper** (injected like the acceptors; the editor's writes `"kept"` to the provenance file) first, then `KeepJob` (the other files stay in `artgen/out/<set>/`), the manifest is written after each row. A failed Keeper leaves its row live, the earlier rows kept; call again for the rest. Switching is refused after (`artgen_no_variant`); no un-keep: make a new set. A row not live or replaced by another set is `invalid_params`. **A set that runs is `artgen_busy`** (a more-run would grow a kept row; the panel's Keep is off, `ArtGenKeepBlocker`). An agent never keeps: the person sees the scene first.

## The panel

`ArtGenSetPanel` is a helper `ArtGenWidget` owns; it keeps no state of a set and calls the session functions the control methods call, so the two cannot differ.

- **Set tab.** Subject rows (one at first, **Add** for more up to `kMaxSetJobs`): **Subject** and **Name** (the job's name; it follows the subject until typed by hand, `ArtGenRowSubjectEdited`, `ArtGenNameFromSubject`), a remove button. `ArtGenSubjectsFromRows` makes the jobs (an empty-subject row skipped, errors name the row). Rows are fixed buffers, so a steady frame allocates nothing. Then an optional set name and **Queue**: the preset of the form above the tabs (by name when saved and unchanged, else a draft, as Generate decides) with the **Set tab's own** count and seed, not the Generate tab's (random: the session draws the set's) and, for an image preset, the reference slot as the set's `reference`; a line under the rows says what goes (`FormatArtGenQueueReference`). While a single run is in work a line says the set starts after it. A refusal is shown under the rows and keeps them; a queued set clears the rows and the set name, so Queue is off until the next subject is typed: while it is off (`ArtGenQueueBlocker`) the reason is its tooltip and a line beside it.
- **Sets and rows.** Under Queue the sets newest first (five lines, then it scrolls); a click selects, a queued set is selected, and when the selection is gone the newest is. The selected set shows its reference (file, mode, weight; the path as the tooltip), its progress bar, **Cancel set**, **Retry** and **More variants** with its count (each says why it is off in its tooltip, `ArtGenMoreBlocker`; the answer names the rows left out), then one row per job in a child at least twelve lines high. Every row has the height of a full one (the thumbnail lines follow the **largest variant count**), so the `ImGuiListClipper` knows where row n is. A row shows name, subject, state, a badge (`live, N alternatives`, `replaced by <set>`, or `kept`), a status line (`StatusOf`, with the file `ArtGenGameFile` names, so it cannot drift from what Accept writes), a failed or cancelled row's reason with **Retry**, a live row's **Keep**, an accepted row's **Use as reference** (the game's sprite, not the draft; not on a replaced row), then the variants as thumbnails over the checkerboard with the seed under them and the verdict mark of its checks (`ArtGen/CLAUDE.md`, "The panel"). A click calls `PickVariant` (on a live row it **switches** the game, so the scene follows at once, and a failed write is shown under the rows); a kept or replaced row's cells do not react to a click; a picture on disk shows **Use as reference** over its corner on hover; a sound variant is a tile with Play / Stop on the widget's player (one sound at a time, the `Ui` bus). A file the manifest names that is gone draws a gap.
- **Pictures live with the rows on screen.** A row loads its thumbnails (and clips) when the clipper shows it and evicts them (`ResourceManager::Evict`) in the frame it is not drawn; all go when another set is selected, the tab is hidden or the panel dies.
- **Footer.** `N live, M kept, R replaced` (`picked / total` before an accept), **Accept all** (`AcceptSet`; off with the reason as its tooltip while the set runs, a job is not `Done`, a sound row has no pick or every row is kept or replaced), **Keep all** (a live row that is not replaced exists) and the answer or the error of the last accept, switch or keep. Under it an image set shows its frame (or, for a legacy set, that its files are full frames).
