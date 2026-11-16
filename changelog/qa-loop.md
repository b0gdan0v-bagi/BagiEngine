# No autonomous QA: a monkey loop with invariants, shrinking and bug entries

- Version: 0.1.8
- Entry: qa-loop
- Date: 2026-11-16
- Bump: patch

## Added
- `tools/qa/bagi_monkey.py`: a seeded monkey runner over the control API; a game's moves come from its `monkey_moves` hook (BagiStress has one).
- The QA loop: a seed log, the signature of a reproduction with a duplicate rule, the command line `next-seeds` / `log-add` / `dupes` / `sweep`, and the skill `/qa-loop`; a found bug becomes an ordinary `bug-*` entry for `todo-loop`.
- `be_add_game_scenario_tests(... INVARIANTS)`: a replay ctest of the stored reproductions (`BagiStressQaReplay`).
