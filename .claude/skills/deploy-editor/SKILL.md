---
name: deploy-editor
description: Deploys the web editor or demo page of this project to its site page through tools/web/build_and_deploy.py (a target of web/targets.json that has `site` and `page`) - a dry run first, then the deploy in the background (the alltests gate, the build, the tar, the upload), and reports in Russian the commit, the gate, the size, the site's answer and the URL. Run ONLY on the owner's explicit word; never run by the loop or offered by another skill. Use when the owner says "задеплой эдитор", "deploy editor", "выложи демо", "выложи эдитор на сайт".
---

# deploy-editor

`/deploy-editor [target=<name>]`. You run the deploy script and report what it did; you change no code. The script does the work (`<engine>/tools/web/DEPLOY.md`: the gate, the fetch, the build, the upload); you pick the target, wait for the script and read its status file.

Terms: `<engine>` is `.` in the engine and `engine` in a game. `<root>` is the root of the repository you run in (the game root in a game); run from it. `python3` is any Python 3.10+ that works on this machine (when `python3` fails on `pyexpat`, use the newest `python3.N` that does). `<scratchpad>` is the session scratchpad. Ask and report in Russian.

## Steps

1. **Pick the target.** List the entries of `<root>/web/targets.json` that have both `site` and `page`. None: say so and stop. One: take it. Several: ask which (in Russian), unless the owner named one.
2. **Dry run:** `python3 <engine>/tools/web/build_and_deploy.py --target <name> --deploy --dry-run` (without `--deploy` the dry run prints only the git and cmake steps, none of the upload; with both nothing is run, sent or read). Show the plan in short: the site, the page, what would be uploaded, the variable and the `.env` the token comes from. The commit and the size come from the real run.
3. **Deploy in the background** (`run_in_background`):
   `python3 <engine>/tools/web/build_and_deploy.py --target <name> --deploy --status-file <scratchpad>/deploy-<name>.json > <scratchpad>/deploy-<name>.log 2>&1`.
   The first run builds a checkout and may install the SDK (minutes, about 2 GB): say so before you start. A gate that has no green full run for the commit starts one (`alltests`), which takes longer. Do not poll; wait for the notice.
4. **Report** in Russian, short, from the status file and `grep` of the log (never the whole log): the commit (`commit`), the gate (green already, or the run it started: the `alltests gate:` lines), the size (the line with the files and bytes), the site's answer (`answer`), the URL (`url`). A failure names the stage (`stage`), `error` and the site's `detail`. A red gate names the failed tests; nothing was uploaded. A missing token: the script's refusal names the variable and the `.env`; pass that on and ask the owner to put the token there (you do not).

## Rules

- No `git push`, `pull`, `fetch` or `merge`; nothing of Railway and nothing of the site's code or settings.
- Never read, print, write or ask for the token: only the script reads it.
- No `--skip-alltests` (the target refuses it) and no free options beside `--target`, `--deploy`, `--dry-run` and `--status-file`.
- Not run by `todo-loop`, `todo-merge` or any other skill, and never offered by them.
