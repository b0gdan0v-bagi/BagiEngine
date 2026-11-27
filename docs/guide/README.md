# BagiEngine guide

A guide for a person who is new to the engine. Each page is one task, done from start to end, with the commands you type and what you should see. The [README of the repository](../../README.md) says what the engine is and how to build it; start there if you have not built it yet.

## How to read a page

- Every page is a task. Do the steps in order; each step says why it is there.
- A code or XML block is an excerpt of a real file of the repository, and the line under it names the file. So it is the same text the engine runs, and it stays right when the file changes. A block with `...` on a line of its own leaves lines out.
- A block of shell commands is meant to be typed as it stands, from the root of the repository.
- The pictures are taken by the engine itself, from a running game, not drawn.
- The pages of the engine's own contracts live next to the code, in the `CLAUDE.md` files. They are written for coding agents, and they are the place to look up an exact rule.

## Pages

1. [Run the demo](run-the-demo.md): build once, run `BagiDemo`, open the editor, change a field, save, and see what changed on disk.
2. [Your first scene](first-scene.md): write a scene file by hand, a background, a title and a sprite, and see it in the editor.

Planned, not written yet: a UI screen, a prefab, a tween and a timeline, config and reflection, input actions, save games, the editor in depth, the web build, the control API from Python, a test, working with agents, the C++ rules explained, where the data lives.

## Where to start

**You make scenes and screens (a designer, an artist).** Start with "Run the demo", then "Your first scene". Planned next: a UI screen, a prefab, a tween and a timeline, and the editor in depth.

**You write Python (tools, bots, tests).** "Run the demo" shows the game you will drive. Planned next: the control API from Python, a test, and where the data lives.

**You work with coding agents.** "Run the demo" and "Your first scene" show what an agent changes when it edits content. Planned next: working with agents, and the C++ rules explained.

## A game of your own

The engine knows no game. A game is a repository of its own that carries the engine as a submodule: see [NEW_GAME.md](../../NEW_GAME.md). The pages here use the engine's own demo, `BagiDemo`, so nothing needs a game repository.
