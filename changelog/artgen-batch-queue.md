# Art generation: one subject per run, so a set is made by waiting at the panel (no queue, no review of a set)

- Version: 0.3.2
- Entry: artgen-batch-queue
- Date: 2026-11-17
- Bump: patch

## Added
- Art generation sets: a queue of jobs in `ArtGenSession` (one preset, a list of `name: subject` lines, `count`, one shared first seed), handed to the service one job at a time over `artgen_poll`; a failed job does not stop the set.
- Sets are kept on disk in `<game root>/artgen/out/<set>/<job>/` with `manifest.json` (format 1, `Widgets/ArtGen/Sets/ArtGenSet.h`, `ArtGenSetStore.h`); an editor restart loses nothing and shows interrupted jobs as `Cancelled`.
- Control methods `artgen_enqueue`, `artgen_set`, `artgen_pick`, `artgen_retry`, `artgen_accept_set` (Accept all: every row checked first, then accepted in order; repeatable after a failure halfway); `artgen_cancel {job, set?}` and `{set, all: true}`; `artgen_state.sets`.
- The Generate panel has Set (subjects, Queue, the queue with Cancel and Retry) and Review (rows by variants, a pick per row, Accept all) tabs.
- The artgen bridge takes optional `outDir` and `name` in a poll's `request` (validated, under `<game root>/artgen/out`).

## Changed
- `artgen_generate` and `artgen_setup` are refused as `artgen_busy` while a set is active. Adapt: a client that runs single subjects waits for `artgen_state.sets` to show no `Active` set, or queues a set instead.
- The `artgen` skill makes a set through `artgen_enqueue` instead of a loop of runs. Adapt: nothing in a game.
