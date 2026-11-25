# tools/sync

`engine_sync.py` is the read-only helper of the skill `sync-engine` (`.claude/skills/sync-engine/SKILL.md`; the branch model: `NEW_GAME.md`, section 1): it reads the git state of the engine repository so that the skill's decisions (stop on divergence, what is ahead of `main`, which pushes to print) are tested code and not prose. Standard library only, Python 3.10+; tests in `tests/` (pytest over real repositories made in a temp folder: a bare "origin", a seed and a work clone; no network).

```bash
python3 tools/sync/engine_sync.py --repo . status [--json]   # in the engine's own repository, where the skill runs
python3 tools/sync/engine_sync.py --repo . pushes            # one 'git push origin <branch>' line per local branch ahead of origin or without one
.venv/bin/python -m pytest tools/sync/tests                             # in the engine
```

**It never writes and never fetches.** Only `for-each-ref`, `rev-list`, `log`, `merge-base --is-ancestor`, `rev-parse`, `worktree list` and `config --get-all` run, so the skill fetches first and the report is as fresh as that fetch. Exit code of `status`: 0 all fine, 2 a participating branch is `behind` or `diverged` from its `origin` (the skill stops), 1 an error (not a repository, no `main`).

## What `status` reports

- **Participating branches:** `main` and every `games/*` (local or on `origin`) named in the local git config of `<E>`, key `engine-sync.include` (multi-valued: `git config --local --add engine-sync.include games/mahjong`). `engine-sync.exclude` names the ones that stay out. A `games/*` in neither list is `undecided`: not analysed, listed for the skill to ask about, so the question is asked once per machine. The config is the owner's per PC; nothing is assumed.
- **Per branch:** the local and `origin` tips and the `relation`: `equal`, `ahead`, `behind`, `diverged`, `no-local` (only `origin` has it; the skill makes the local branch, nothing local can have diverged), `no-remote` (a push line is printed for it). `worktree` is the folder that has the branch checked out and `held_elsewhere` says it is not this checkout (a branch another worktree holds cannot be switched to); `linked_worktree` says this checkout is a worktree of a worker, where the skill does not run. `changelog_driver` is whether the git merge driver of the generated version header and `CHANGELOG.md` is in the local config (`tools/changelog/CLAUDE.md`, "Merging"); when it is not, the skill runs `changelog.py install-driver`.
- **Per `games/*` against `main`** (tips: the local branch, else `origin`'s): `ahead_of_main`, `behind_main`, and from `git log --first-parent main..<branch>`: `merges` (the subjects of the todo branches merged into it, oldest first), `commits` (plain first-parent commits) and `sync_merges`. A first-parent merge whose other parents are all reachable from `main` is a **sync merge** (`main` taken into the game): it is no work of the game, so `own_work` (merges or commits) stays false and `Up` leaves the branch alone, otherwise each `Down` would make the next `Up` merge an empty branch back.
