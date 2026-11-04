---
status: in progress 2026-11-04 branch todo/shared-build-format-skills
---
# The build, clang-format and strip-pch-includes skills are still two copies

**Where:** `engine/.claude/skills/{build,clang-format,strip-pch-includes}/SKILL.md` and the copies `.claude/skills/{build,clang-format,strip-pch-includes}/SKILL.md` of BagiEngine-Chess (55 to 104 differing lines each).

**What is missing:** the `todo-*` skills and agents are now one copy in the engine, linked into the game by symlinks, with what differs between the repositories in `.claude/rules/todo-project.md`. The three skills for build, format and includes were left as they were, so every fix to them still has to be made twice and the copies drift apart (different presets and targets, `engine/` path prefixes, the `--root .` of the format scripts).

**A fix has to cover:**

- one text in `engine/.claude/skills/`, using the `<engine>` and `<R>` terms of `todo-loop`, with the game's build targets, presets note and format roots in the `todo-project.md` of each repository (or a rules file of its own if that fits better);
- the copies in `.claude/skills/` of BagiEngine-Chess replaced by symlinks, after checking in a fresh session that Claude Code follows them;
- the `README.md` and `CLAUDE.md` build commands of the game checked against the shared text;
- `engine/CLAUDE.md` ("Rules and Skills") and `.claude/rules/engine-and-todo.md` of the game updated (the sentence "`build`, `clang-format` and `strip-pch-includes` are still copies" goes).
