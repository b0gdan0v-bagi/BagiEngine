# tools/tokstats

`tokstats.py`: what a todo entry cost in tokens, per role, next to how its work went, and a history of it that outlives the Claude Code transcripts (they are cleaned after a while). Standard library only; the store is read through `tools/todo/todo.py`.

```bash
python3 <engine>/tools/tokstats/tokstats.py entry <branch-id> [--entry ID] [--json]   # look, write nothing
python3 <engine>/tools/tokstats/tokstats.py record <branch-id> [--entry ID]           # todo-merge, a stopped task
python3 <engine>/tools/tokstats/tokstats.py backfill [--since YYYY-MM-DD]            # every branch not recorded yet
python3 <engine>/tools/tokstats/tokstats.py report [--since YYYY-MM] [--area A] [--json]
```

Run it from the checkout the loops ran in (`--root`): its transcripts and those of its worker folders `<root>.workers/w<k>` are read; a game's root also covers its engine entries, and git is asked in the root and in `engine/`.

## Attribution

- A role is the subagent's `agentType` (`todo-classifier`, `todo-architect`, `todo-developer`, `todo-reviewer`); a main session is the `conductor` (in the main checkout that is also the owner's session, `todo-merge` included); any other subagent is `other`.
- An answer counts for `todo/<branch-id>` when its record's `gitBranch` names it. Otherwise a main session holds the branch its last Bash command named (`claim --branch`, `switch`, `review --branch`) until a `todo.py review`, `stop` or `close`; what it does before a claim (choosing) belongs to no entry. A subagent counts whole for the branch its records name most, else for the one its session held when it started: a game session working on an engine entry stands on the game's branch.
- Weighted cost: input 1, cache write 1.25, cache read 0.1, output 5 (the context re-read is what a long run pays). Dollars: the price table of the worker panel, `<claude home>/todo/prices.json` replaces it.

## The record

`<claude home>/todo/tokstats/<branch-id>.json` (`TOKSTATS_DIR`): area, totals and per role (runs, turns, peak and mean context, tokens, weighted, dollars, models, tool output by tool and Bash kind), the store's `reviewRounds`, each review plan's verdict and blocking findings by kind (`[blocking:<kind>]`, `todo-reviewer.md`; older reviews count as `unknown`), developer runs that ended `WP incomplete`, changed lines, and the plan's `## Files` against the changed files. A record made after the transcripts are gone keeps the tokens of the earlier one. The history is local to the machine: the store has no plan kind for it yet.
