# Art generation sets

## Overview

A **set** is one preset applied to a list of subjects: one **job** per subject, all with the same first seed so the style matches. The model (`ArtGenSet.{h,cpp}`) is plain data over the manifest: no ImGui, no `ArtGenSession`, no `ControlService`, so another reader of the manifest (`artgen-live-alternatives`) can use it without a session. Editor only, like the rest of `ArtGen/`.

| File | Purpose |
|------|---------|
| `ArtGenSet.{h,cpp}` | `ArtGenSet`, `ArtGenSetJob`, `ArtGenSetVariant`, `ArtGenSetJobState`; the subject-line rules; the manifest to and from JSON; the counting helpers, `FindJob`, live and kept (`IsLive`, `AlternativesOf`, `KeepJob`) |
| `ArtGenSetStore.{h,cpp}` | The sets on disk, no session: the folder layout, `WriteManifest`, `LoadSets`, the manifest's relative paths |
| `ArtGenSetPanel.{h,cpp}` | The Set and Review tabs of the Generate panel, ImGui over the session: subjects, Queue, the sets with progress and buttons; the review of one set |

## Names and subjects

- A set name and a job name are 1 to 64 of `A-Z a-z 0-9 _ -` (`IsValidSpriteName`): the set name is a folder, the job name is a folder, the file stem, and the sprite (or clip) name of Accept all. Job names are unique in a set; at most `kMaxSetJobs` jobs; `count` is 1 to `kArtGenSetMaxCount`.
- **Subject lines** (`ParseArtGenSubjects`; the panel field and the API's one string): one job per line, trimmed; an empty line and a line that starts with `#` are skipped. `name: text` names the job when the text before the **first** `:` is a valid name (so it has no space: `a cat: sitting` is one subject); otherwise the name is `ArtGenNameFromSubject` (`White Knight!` -> `white_knight`, cut at 64). A name with nothing after it, a subject with no letter or digit, a name used twice and a 65th job are errors that name the line. Subjects that arrive as data (a control call) go through `CheckArtGenSubjects`.
- `DefaultArtGenSetName` is `<preset>-<yyyymmdd-hhmmss>` in UTC (a name does not depend on the machine's zone); the preset part is made safe and cut so the whole stays a valid name. `created` and `accepted.date` are `ArtGenTimestamp` (`2026-11-17T12:34:56Z`), so sorting the text sorts the time.

## Job states

`Pending`, `Running`, `Done`, `Failed` (with `error`), `Cancelled`. A set is **active** while a job is `Pending` or `Running` (`IsActive`). A failed or cancelled job never stops the set. A `Done` job may have a **pick**, the seed of one of its variants (`PickVariant` refuses a seed it does not have).

## The manifest

`<game root>/artgen/out/<set>/manifest.json`, format `kArtGenManifestFormat` (1). Only `WriteManifest` writes it (the session calls it at every change of a job); readers use `ArtGenSetFromJson`.

```
{"manifest": 1, "set", "created", "kind" ("image"|"audio"), "preset", "presetData" (a draft's content, else null),
 "count", "seed" (the first seed of every job),
 "jobs": [{"name", "subject", "state", "error", "variants": [{"seed", "file", "provenance", "kind"}],
           "pick": seed|null, "accepted": null | {"as", "date", "kept"}}]}
```

A job with `accepted` and not `kept` is **live**: the game holds its pick and the other variants are alternatives to switch to. `kept` is written always; a manifest from before it existed has `accepted` without the key and reads as **kept**, so nothing becomes live by itself. `KeepJob` fixes a live job (`kept`, the date, the variants cut to the pick; the files stay on disk).

`file` and `provenance` are relative to the set folder with `/`, so the folder can be moved. Reading is checked (`Control/Json.h`): a wrong type, a bad name, an unknown job state, a duplicate job name, a `pick` that is not a seed of the job's variants, or a `manifest` number other than 1 is an error that says where (`jobs[2].variants[0]: 'seed' must be an integer`), never an abort; unknown keys are ignored, so a later format 1 writer can add keys.

## On disk

`<game root>/artgen/out/<set>/<job>/<job>_<seed>.png|flac` (+ `.artgen.json`) and `<set>/manifest.json`; the wire's `outDir` of a job is `<set>/<job>` (`OutDirOf`). The folder is git-ignored in a game and kept for review; nothing deletes a set.

- `WriteManifest` makes the folders and replaces the file atomically (`WriteTextFileAtomically`), so a crash leaves the old manifest or the new one. It refuses a bad set name, so no path leaves `artgen/out/`. `NewSetFolderFree` is false for a name whose folder exists, with or without a manifest: a new set never writes into the files of another, nor over a manifest that did not parse.
- `LoadSets` reads every `artgen/out/*/manifest.json`, oldest first (`created`, then name). **The folder's name is the set's name** (a moved folder still loads). A job that was `Pending` or `Running` comes back `Cancelled` (`kEditorClosedReason`) in memory; the file is not rewritten until the next change. A manifest that does not read (not JSON, wrong type, another format number) or a folder with an invalid name is skipped with one warning and never touched. A file a variant names that is gone stays in the set: the review shows a gap.
- `RelativeToSet` turns the real path the service reported into the manifest's form: relative to the set folder, `/` also from a `\` path; a path outside the folder is kept as given (with `/`). `AbsoluteInSet` is the way back: relative joins the set folder, an absolute path stays as it is, a path with `..` is refused (empty), so a hand-edited manifest cannot make Accept all read outside the set by accident.

## The queue

The session (`ArtGenSessionSets.cpp`) runs the sets through the one run it already has; nothing changes for the service, whose poll gets the usual `request` with two more keys, `outDir` and `name`.

- **One job at a time, first in first out.** `PumpSets` starts the first `Pending` job of the first set in the queue (`_setQueue`: sets in the order they were enqueued or retried; a retried set with nothing waiting goes behind the others) as an ordinary `_run` (`outDir` `<set>/<job>`, `name` the job's name, the set's preset, count and shared seed). It runs from the poll (before the hand-over, so a job goes out in the poll that finds the session free) and from the end of a run, never from `EnqueueSet`: **no poll, no job**, they wait like a queued run. It also waits for a run, a setup action or a service that is silent or whose generator is not `ready` (the jobs stay `Pending` and the first poll with a ready generator starts one).
- `IsBusy()` is true while a run is in work or any set is active, so `Request`, `RequestSetup` and `artgen_generate` refuse meanwhile; `EnqueueSet` does not (a second set joins the line).
- **A run ends a job** (`EndSetJob`): `Done`, `Failed` (the service's reason) or `Cancelled`, the manifest is written and the next job is pumped. A report of an older run is stale as before and touches no job. `artgen_image` appends the variant to the job (paths relative to the set folder, a seed seen again replaces) and writes the manifest.
- **Cancel.** A `Pending` job is cancelled at once; the running job through `Cancel` (a run the service has not taken ends at once, else the `cancel` of the next poll and the end the service reports); cancelling a set cancels its pending jobs first and then the running one, so the line moves on to the next set. **Disconnect** ends only the running job (`Cancelled`, "the service was stopped"); the others stay `Pending` and go when a service polls again.
- **Retry** puts `Failed` and `Cancelled` jobs back to `Pending` and forgets the earlier try (variants, pick, accepted): the new one writes the same files under the same seeds.
- **The set's kind** comes from the service's catalog, which is empty until the first poll, so `EnqueueSet` only guesses (`image`). `StartJob` reads the catalog again, and the kind of a variant the service reports overrides it (`RecordSetImage`), so a sound set queued early is still accepted as clips.
- **Game root.** `SetGameRoot` reads the sets of the game only when the root changes (the panel calls it at every switch of the run mode, and a reload would turn a running job into `Cancelled`). The shared seed is drawn at `EnqueueSet` when none is given, from the service's own range, and written to the manifest. A default set name has a resolution of a second: a taken one gets `-2`, `-3`, ... (`DefaultArtGenSetName` with an attempt cuts the preset part to keep 64); a name the caller gave and is taken stays `invalid_params`.

## Control methods

The table is in `ArtGen/CLAUDE.md`; the handlers are in `ArtGenSessionSets.cpp`. All answer at once and keep nothing on a refusal. `artgen_enqueue` is refused only for what makes a bad set (`invalid_params`: a bad preset, subject, count, seed or name, a taken folder); it never says `artgen_busy`, a second set joins the line. `artgen_cancel` with `job` and no `set` takes the one set that has that job `Pending` or `Running`; two such sets is `invalid_params`, so an agent that did not queue the set cannot cancel it by a name that happens to match without naming it. `artgen_pick` is allowed on any job that has the variant (the Accept all check, not the pick, wants `Done`); on a live row it also writes the game.

## Accept all

`ArtGenSession::AcceptSet` (`artgen_accept_set`) writes a **candidate of every job** into the game under the **job's name**, through the acceptors the single Accept uses (an image as the sprite, a sound as the clip `assets/artgen/<name>.wav` with default settings; no node, a set has one kind), so the two paths cannot drift. The candidate is the pick, else, for images only, the first variant, which becomes the pick. A sound still needs a pick on every row: only a person listens. It writes the game's assets and `TextureLibrary.xml` once per row, so it is checked as a whole first and nothing is touched by a refusal:

1. The set exists and is not active (`artgen_busy`); every row that is neither kept nor replaced has a candidate and is not failed or cancelled (`artgen_set_incomplete` names the rows not picked, failed or cancelled: a job cannot be dropped, it is retried first).
2. Every row to write: a valid name, the candidate's file of the set's kind, its provenance when the manifest names one (the file system is asked, the manifest is not trusted), and an acceptor installed (`artgen_accept_failed`).
3. Then the rows in order; after each one the job gets its pick, `accepted{as, date, kept=false}` and the manifest is written. A failure halfway returns the row, its position and the rows done, and stops: the marks of the rows done stay, and **calling it again is the repair**, an accept replaces the same name, so nothing is ever half old, half new beyond the one row that failed. A **kept** row is skipped (`{job, kept: true}` in the answer), and so is a **replaced** one (`{job, replacedBy: <set>}`, see below): skipped, not refused, so the rows nobody replaced still go in and the answer says what was left. A row already live is written again from its current pick. A written row's answer adds `seed` and `alternatives`.

**Live rows and switching.** A written row is live: the game holds its pick, the job's other variants are alternatives. `PickVariant` (`artgen_pick`) on a live row **switches**: it checks the variant and its files, writes it through the acceptor of the set's kind under `accepted.as`, and only then sets `pick` and `accepted.date`, so a failed write (`WriteFailed`, `artgen_accept_failed`) leaves the manifest as it was; the pick the game already holds is not written again. `artgen_pick` adds the acceptor's answer as `switched`. On a row that is not accepted a pick is bookkeeping only; on a kept row it is `artgen_no_variant` (also for a manifest from before Keep that still lists the other variants), the pick it holds being no change. A switch does not check that the set is idle: a live row exists only in a finished set.

**Replaced rows.** The game has one file per name, so two sets can hold a live row under the same name (the skill queues a new one-subject set of a row that does not fit). The set whose row of that name and kind has the later `accepted.date` owns the file (`FindNewerOwner`; the dates have a resolution of a second, a tie goes to the set later in the list, which is the order of creation). The other row is **replaced by** that set: a switch is `artgen_accept_failed`, Keep of it `invalid_params`, both naming the set, since either would overwrite the newer candidate or record a seed the game does not hold. Keep all skips such rows and lists them in `replaced: [{job, by}]`. A set that accepts the name after that owns it.

## Keep

`KeepSet(set, job?)` (`artgen_keep`) makes a live row final: the **Keeper** (an injected function like the acceptors, so the session stays free of the file system; the editor's writes `"kept": {"seed", "date"}` into the game's provenance file of the sprite or clip) runs first, then `KeepJob` (`accepted.kept`, the variants cut to the pick, the files of the others stay in `artgen/out/<set>/`), and the manifest is written after each row. A failure of the Keeper leaves its row live, the rows before it kept, and calling again keeps the rest. Switching is refused after (`artgen_no_variant`); there is no un-keep, the way back is a new set. A row that is not live (not accepted, or already kept) or is replaced by another set is `invalid_params`. An agent never keeps: the person looks at the scene first.

## The panel

`ArtGenSetPanel` is a helper `ArtGenWidget` owns; it keeps no state of a set, only the text of its fields and the last message, and calls the session functions the control methods call, so the two cannot differ.

- **Set tab.** A subjects field (the line rules above), an optional set name, **Queue**: the preset of the form above the tabs (by name when saved and unchanged, else a draft, as Generate decides) with the **Set tab's own** count and seed, not the Generate tab's (random: the session draws the set's). While a single run is in work (Generate tab) a line says the set starts after it. A refusal is shown under the field and keeps the text; a queued set clears both fields. Below, the sets newest first: state, `done/total`, a progress bar for the job in work, **Cancel set**, **Retry**; the jobs of an active set as rows with their state, the reason of a failure and a cancel button on `Pending` and `Running`.

- **Review tab.** Opened by **Review** on a set (kept by name; the tab can show a set that is still running). One row per job in a child of a fixed height, so the `ImGuiListClipper` knows where row n is: name, subject, state, a badge (`live, N alternatives`, `replaced by <set>` or `kept`), a failed or cancelled row's reason with **Retry**, a live row's **Keep**, then the variants as thumbnails over the checkerboard with the seed under them. A click calls `PickVariant` (the accent frame; on a live row it **switches** the game, so the scene follows at once, and a failed write is shown under the rows); a kept or replaced row's cells do not react; a sound variant is a tile with Play / Stop on the widget's player (one sound at a time, the `Ui` bus). A file the manifest names that is gone draws a gap.
- **Pictures live with the rows on screen.** A row loads its thumbnails (and clips) when the clipper shows it and evicts them (`ResourceManager::Evict`) in the frame it is not drawn; all are evicted when another set is chosen, the review is closed, the tab or the window is not shown, or the panel dies. A set of twelve with four variants holds a few rows of pictures, not 48 textures.
- **Footer.** `N live, M kept` (`picked / total` before anything is accepted), **Accept all** (`AcceptSet`; disabled with the reason as its tooltip while the set runs, a job is not `Done`, a sound row has no pick or every row is kept: an image row needs no pick), **Keep all** (a live row that is not replaced exists) and the answer or the error of the last accept, switch or keep. The same session functions as the control methods, so a failure halfway names the row and pressing again finishes.
