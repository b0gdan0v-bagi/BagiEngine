# tools/alltests

The **full test run**: every test of a repository, the `extended` ones the everyday `ctest --preset <p>` leaves out included (`.claude/rules/build-project.md` says which preset runs what), on one commit of the local base branch. It runs only on demand: the owner's skill `alltests`, before a deploy (the gate below) and in `sync-engine`. A failure files a todo entry and never turns a WP red; nothing schedules it. Standard library only, Python 3.10+.

```bash
python3 tools/alltests/alltests.py                                # the tip of the local main of this repository
python3 tools/alltests/alltests.py --commit HEAD                  # another commit (the deploy gate's hint)
python3 engine/tools/alltests/alltests.py --repo . --area <area>  # a game, for itself, from its root
python3 tools/alltests/alltests.py --dry-run                      # the same run; neither the store nor the results file is written
.venv/bin/python -m pytest tools/alltests/tests                   # fake cmake, ctest and todo CLI over a repository in a temp folder
```

Exit code 0 green, 3 red, 1 an error (nothing recorded). `--preset` is the configure preset (`macos-debug` by default on macOS); the tests run with `<preset>-all`.

## The work folder

`~/.cache/bagi-alltests/<repository>` (`BAGI_ALLTESTS_HOME` moves the parent, `--work-dir` names another): `worktree/` with its `build/<preset>/`, `logs/` (each step's output and ctest's JUnit file, replaced by the next run), `results.json`, and `alltests.lock` (one run at a time; a lock whose process is gone is taken over). `<repository>` is the folder of the main checkout, so every worktree of a repository shares one results file.

- **The worktree is detached** at the commit: it holds no branch, so the owner's checkout, `todo-merge` and `sync-engine` can still switch to `main`. It is updated with `checkout --force`; nothing else in it is anyone's work.
- **Nothing reads a remote.** The base is the local branch (pushes and pulls are the owner's). Each submodule that is initialised in the repository is cloned from that local copy (hard links) and checked out at the commit the tested commit records; one that is not initialised there is left out, and a recorded commit missing from the local copy is an error that says to update it there.
- **The venv is linked** from the repository (`.venv`, and `engine/.venv` of a game), as the loop's workers share theirs: without it the configure takes a system Python that may lack the reflection generator's packages.
- The build is `cmake --preset <p>` and `cmake --build --preset <p>` (every target, so the smoke tests that need the product run), then `ctest --preset <p>-all --output-junit`. The failed names are run once more with `--rerun-failed`: one that passes alone is reported as flaky and is not a failure, as a name that fails under load is not a finding until it fails alone.
- Skipped tests (exit 77, disabled) are counted and named in the summary line and the result. A skipped test labelled `smoke` makes the run red and is filed: it skips when its product or Python is missing, so a green run would have proved nothing of the product.
- The JUnit file is read with patterns, not `xml.etree`: a Homebrew Python can lack a working `pyexpat`.

## The results file and the deploy gate

`alltests_results.py` owns `results.json`: per commit `status` (`green` / `red`), `preset`, `finished`, `failed`, `flaky`, `skipped`, the entries filed; and `failing`, each failing test with the commit it was first seen failing at. It is written through a rename, so a deploy never reads half of it. `--dry-run` does not write it.

`tools/web/build_and_deploy.py` and `tools/rooms/build_and_deploy.py` import it: `--deploy` stops (exit 2, before any build) unless HEAD of `--source-dir` has a green result, and the message says how to get one; `--skip-alltests` deploys anyway and prints that it did; `--dry-run` only reports the gate; `--alltests-results` names another file. The scripts build the working tree, so a tree that `git status` shows changed is refused too, except the untracked files inside the folders the script writes itself (its stage, the web's `dist/`).

## The todo entries

Through the todo CLI (`tools/todo/todo.py --actor alltests`, run in the repository, so its `.env` holds the token; `--area` is the area, `engine` by default):

- One entry `alltests-<test>` per test that failed twice (characters outside `[A-Za-z0-9_-]` become `-`; a long name keeps a hash). A failing configure or build is `alltests-build`; more failing tests than `--max-entries` are one entry `alltests-many-failures` that lists them.
- The body: the commit, the first commit seen failing, the commits since the newest green ancestor (`git log`), the command that reproduces it and the tail of the second run's output.
- A failure of a test that has an entry updates its body (`edit --expected-version`); a closed one is reopened. A green run never closes an entry: the fix's merge does.
