# Art generation sets

## Overview

A **set** is one preset applied to a list of subjects: one **job** per subject, all with the same first seed so the style matches. The model (`ArtGenSet.{h,cpp}`) is plain data over the manifest: no ImGui, no `ArtGenSession`, no `ControlService`, so another reader of the manifest (`artgen-live-alternatives`) can use it without a session. Editor only, like the rest of `ArtGen/`.

| File | Purpose |
|------|---------|
| `ArtGenSet.{h,cpp}` | `ArtGenSet`, `ArtGenSetJob`, `ArtGenSetVariant`, `ArtGenSetJobState`; the subject-line rules; the manifest to and from JSON; the counting helpers |
| `ArtGenSetStore.{h,cpp}` | The sets on disk, no session: the folder layout, `WriteManifest`, `LoadSets`, the manifest's relative paths |

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
           "pick": seed|null, "accepted": null | {"as", "date"}}]}
```

`file` and `provenance` are relative to the set folder with `/`, so the folder can be moved. Reading is checked (`Control/Json.h`): a wrong type, a bad name, an unknown job state, a duplicate job name, a `pick` that is not a seed of the job's variants, or a `manifest` number other than 1 is an error that says where (`jobs[2].variants[0]: 'seed' must be an integer`), never an abort; unknown keys are ignored, so a later format 1 writer can add keys.

## On disk

`<game root>/artgen/out/<set>/<job>/<job>_<seed>.png|flac` (+ `.artgen.json`) and `<set>/manifest.json`; the wire's `outDir` of a job is `<set>/<job>` (`OutDirOf`). The folder is git-ignored in a game and kept for review; nothing deletes a set.

- `WriteManifest` makes the folders and replaces the file atomically (`WriteTextFileAtomically`), so a crash leaves the old manifest or the new one. It refuses a bad set name, so no path leaves `artgen/out/`. `NewSetFolderFree` is false for a name whose folder exists, with or without a manifest: a new set never writes into the files of another, nor over a manifest that did not parse.
- `LoadSets` reads every `artgen/out/*/manifest.json`, oldest first (`created`, then name). **The folder's name is the set's name** (a moved folder still loads). A job that was `Pending` or `Running` comes back `Cancelled` (`kEditorClosedReason`) in memory; the file is not rewritten until the next change. A manifest that does not read (not JSON, wrong type, another format number) or a folder with an invalid name is skipped with one warning and never touched. A file a variant names that is gone stays in the set: the review shows a gap.
- `RelativeToSet` turns the real path the service reported into the manifest's form: relative to the set folder, `/` also from a `\` path; a path outside the folder is kept as given (with `/`). `AbsoluteInSet` is the way back: relative joins the set folder, an absolute path stays as it is, a path with `..` is refused (empty), so a hand-edited manifest cannot make Accept all read outside the set by accident.
