---
name: context-sanitizer
description: Audits and cleans the context Claude Code loads by itself in this repository (the root CLAUDE.md, rules without paths, nested CLAUDE.md files, memory) - measures it, finds what bloats it, moves detail into lazily loaded nested docs and path-scoped rules, compresses what stays, fixes the references and checks the budget. Use when the user says the context is too big, CLAUDE.md is over the limit, asks to clean or slim the docs or the initial context, says "контекст санитайзер", "почисти контекст", "почисти CLAUDE.md", or when check_doc_budget.py fails.
---

# context-sanitizer

`/context-sanitizer [audit | fix] [<path>...]`. Default `audit`: measure and report, change nothing. `fix`: do the cleanup after the owner agreed to the plan. Paths narrow the scope (a folder, one `CLAUDE.md`).

The terms `<engine>` (`.` in the engine, `engine` in a game) and `<R>` (the repository: `.`, or `engine` for the engine from a game checkout) are those of `.claude/rules/build-project.md`. Explanations to the user are in Russian when the user writes in Russian; the docs stay English.

## What costs context

| Loaded | When | Budget (`<engine>/CI/scripts/check_doc_budget.py`) |
|--------|------|------|
| root `CLAUDE.md` of the session's folder | every session and every agent (`todo-*` too) | 12k characters |
| `.claude/rules/*.md` **without** `paths:` front matter | every session | 8k in total |
| `MEMORY.md` of the user's project memory | every session | a line per memory |
| a nested `CLAUDE.md` | when a file under its folder is read (its ancestors too) | 20k each |
| `.claude/rules/*.md` **with** `paths:` | when a matching file is read | none, but keep them focused |
| any other `.md` (e.g. `AuthoringDecision.md`) | only when read explicitly | none |

A rules file that a skill or an agent reads by path (`build-project.md`, `todo-project.md`) needs no auto-loading: give it narrow `paths:`.

## Steps

1. **Measure.** `python3 <engine>/CI/scripts/check_doc_budget.py --root <R>` (and `--root .` inside `engine/` for a game). For every file over budget, the size of each section: `awk '/^#+ /{if(h)printf "%7d  %s\n", n, h; h=$0; n=0} {n+=length($0)+1} END{printf "%7d  %s\n", n, h}' <file>`. Count the memory index lines. Estimate tokens as characters / 4.
2. **Diagnose** what fills the big sections. The usual bloat, in the order it pays off:
   - a catalogue of tests (a paragraph per test file restating its checks) — the test names say it;
   - history and measurements ("measured 186 of 240", "it was 0.6 s", "the first try lost") — they belong to the entry's plan in the todo store;
   - values of named constants, pixel coordinates and sizes the scene file or a header already holds — name the constant, not its value;
   - a file tree with a long sentence per file — keep a table of one line per file in the folder's own `CLAUDE.md`;
   - one subsystem described in the root because it was written first;
   - the same rule stated in two docs.
   Also find **why** it grew: `git log --oneline -- CLAUDE.md | wc -l`, and whether the `todo-*` agents append to it.
3. **Report** (in `audit`, then stop): a table before → planned after per file, the plan of moves (section → target file), what will be cut, and the risks: unmerged branches that touch the same docs (`git -C <R> branch --list 'todo/*' --no-merged <base>` and `git diff --numstat <base>...<branch> -- '*.md'`), code comments that cite a section. Ask the owner with AskUserQuestion: how hard to cut the test catalogue, move only or move and compress, now or after the open branches merge, which repositories.
4. **Branch.** In `fix`: a branch of its own in each repository touched (`docs/context-cleanup` from the base; never on the base itself). An engine change from a game is a branch of the engine; the game's pointer of `engine/` is moved by `/todo-merge` or the owner, not here.
5. **Move by lazy loading.** Each fact goes to the folder it is about:
   - the root keeps: what the repository is, the state in two lines, a "where to read" table of the nested docs, the rules that hold everywhere, the docs budget;
   - a nested `CLAUDE.md` per source folder with its contracts, files table, gotchas;
   - a flat folder too big for one doc: split a topic into a path-scoped rule (`.claude/rules/<topic>.md` with `paths:` on its files) or a side `.md` the nested doc points at;
   - tests: a `tests/CLAUDE.md` with the helpers and one line per group of files.
   **Keep the section headings that code comments cite** (`git grep -n 'CLAUDE.md' -- '*.h' '*.cpp' '*.py' '*.cmake'`), so `CLAUDE.md ("Scoring")` still resolves from the folder of the citing file.
6. **Compress what stays.** Keep: contracts and invariants, the reason a reader cannot get from the code, owner decisions (one line, with the date), gotchas, pointers to tests that pin a behaviour. Cut: what step 2 lists. Prefer a short bullet over a long sentence; no row of a table longer than two lines.
7. **Fix the references.** `git grep -n '<old section name>\|root CLAUDE.md' -- '*.md' '*.cmake' '*.py' '*.h' '*.cpp'` for every section that moved, and the rules files that describe the doc layout (`todo-project.md` "Shared files", the engine's `CLAUDE.md` "Docs, rules and skills").
8. **Check.** `check_doc_budget.py` exits 0 in every repository touched. Nothing in the vendored zones changed. No test reads a doc (`git grep -n 'CLAUDE.md' -- tests`); if one does, build and run it.
9. **Commit** per repository, one commit per logical step (the split, the guardrails, a big doc), with the Co-Authored-By line the conversation gives. Never push.
10. **Report** a table of the sizes before and after (the always-loaded part separately, in characters and approximate tokens), the commits, the branches that will conflict at their merge, and what was left for later.

## Keeping it clean

- The `todo-*` agents follow the docs budget (`todo-developer` step 4, `todo-architect` "Checkboxes", `todo-reviewer` "Docs"); if they stop doing so, fix the agents, not only the docs.
- The memory is the user's: do not delete memories without asking; a memory that repeats the repository's docs is pointed out, not removed.
- Do not "clean" by deleting an owner decision or a known deviation: move it to the folder it is about, or to the entry's plan in the todo store when it is history.
