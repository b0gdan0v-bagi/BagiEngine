# The skills

You will learn what a skill is, how to start one, and which of the engine's skills do what, grouped by what you want done. Each skill is one line here and a link to its own text, which is the exact rule.

You have read [Ask for a change](ask-for-a-change.md) and [The todo loop](todo-loop.md), so the two skills that file and carry out work are only named here.

## What a skill is and how you start one

A skill is a text file, `.claude/skills/<name>/SKILL.md`, that a coding agent reads and follows. You start one in a Claude Code session opened in the repository, in either of two ways: type `/<name>` with its arguments, or describe the task in your own words and the session picks the skill whose `description` fits. The first line of a `SKILL.md` is that description; the first paragraph says what the skill is for and what it never does.

A game keeps no copy of the skills. It links the engine's by symlink, as [NEW_GAME.md](../../NEW_GAME.md) (section 4) shows. That page links seven: `build`, `clang-format`, `strip-pch-includes`, `todo-loop`, `todo-merge`, `todo-add` and `todo-db`. Any other skill is linked the same way. A skill is changed in the engine, never through the link in a game, because the link points at the engine's file.

## The map

### Keep the tree building and tidy

| Skill | What it does |
|-------|--------------|
| [`build`](../../.claude/skills/build/SKILL.md) | picks the preset for this system, configures only when needed, builds, and reports errors with `file:line` |
| [`clang-format`](../../.claude/skills/clang-format/SKILL.md) | formats the C++ sources by the repository's `.clang-format` |
| [`strip-pch-includes`](../../.claude/skills/strip-pch-includes/SKILL.md) | removes includes that the shared precompiled header already gives |
| [`tidy-fix`](../../.claude/skills/tidy-fix/SKILL.md) | adds braces around the bodies of `if`, `for` and `while`; its text says to run it by hand now and then, not on every commit |

### Ask for work and carry it out

| Skill | What it does |
|-------|--------------|
| [`todo-add`](../../.claude/skills/todo-add/SKILL.md) | files an idea as an entry of the store (the page [Ask for a change](ask-for-a-change.md)) |
| [`todo-db`](../../.claude/skills/todo-db/SKILL.md) | reads and edits the store: list, show, history, the dashboard |
| [`todo-loop`](../../.claude/skills/todo-loop/SKILL.md) | works entries one by one, each on its own branch, and leaves the result in `review` (the page [The todo loop](todo-loop.md)) |
| [`todo-loop-parallel`](../../.claude/skills/todo-loop-parallel/SKILL.md) | starts a loop in every worker folder at once, shows how they are doing, and stops them |
| [`todo-dispatch`](../../.claude/skills/todo-dispatch/SKILL.md) | advises which entry each free worker takes next, by the owner's preferences, and starts them |
| [`todo-merge`](../../.claude/skills/todo-merge/SKILL.md) | shows a report, asks, and merges a reviewed branch into the base |

### Find problems

| Skill | What it does |
|-------|--------------|
| [`qa`](../../.claude/skills/qa/SKILL.md) | plays the game over the control API by a charter, checks each step, and files each bug as an entry with a reproduction; it never fixes code |
| [`qa-loop`](../../.claude/skills/qa-loop/SKILL.md) | the same, unattended for a time budget, walking at random and remembering where it stopped |
| [`perf`](../../.claude/skills/perf/SKILL.md) | runs the benchmarks several times and says per benchmark whether it got slower, faster or is noise |
| [`perf-investigate`](../../.claude/skills/perf-investigate/SKILL.md) | finds where the time and the allocations of one benchmark go, and files what it found; it changes no code |
| [`perf-render`](../../.claude/skills/perf-render/SKILL.md) | finds why a frame draws too much or too slowly (draw calls, batches, overdraw); it changes no code |
| [`alltests`](../../.claude/skills/alltests/SKILL.md) | the full test run with the extended checks, in a worktree of its own; its text says to run it only on the owner's explicit command |

### Help to write

| Skill | What it does |
|-------|--------------|
| [`cpp-senior-programmer`](../../.claude/skills/cpp-senior-programmer/SKILL.md) | a map of which `CLAUDE.md` to read and which file to copy for a C++ task |
| [`artgen`](../../.claude/skills/artgen/SKILL.md) | makes a sprite or a sound through the editor's Generate panel and puts the one you choose into the game |

### Look after the agents' own docs

| Skill | What it does |
|-------|--------------|
| [`context-sanitizer`](../../.claude/skills/context-sanitizer/SKILL.md) | measures what an agent loads by itself, and moves detail into the files read on demand; it only reports until you agree to the plan |

## The owner's alone

`sync-engine` and `deploy-editor` are the owner's: an agent never runs either on its own, and this guide does not describe them.

## Reading a skill before you run it

Open its `SKILL.md`. The first paragraph says what it is for and what it never does, and a skill that writes to the store or to git says so there. The steps below it are what the agent will do, in order.

## What you did

You learned what a skill is and the two ways to start one, how a game links the engine's skills and why a skill is edited only in the engine, and which skill to reach for to keep the tree tidy, to ask for work, to find problems, to write and to look after the agents' docs.

Next (planned): What to review in an agent's work
