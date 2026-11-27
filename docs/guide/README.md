# BagiEngine guide

A guide for a person who is new to the engine. Each page is one task, done from start to end, with the commands you type and what you should see. The [README of the repository](../../README.md) says what the engine is and how to build it; start there if you have not built it yet.

## How to read a page

- Every page is a task. Do the steps in order; each step says why it is there.
- A code or XML block is an excerpt of a real file of the repository, and the line under it names the file. So it is the same text the engine runs, and it stays right when the file changes. A block with `...` on a line of its own leaves lines out.
- The files a page edits live in an example folder, `docs/guide/examples/<page>/`, laid out like a game's content. You copy it into a folder of your own, so the repository stays as it is.
- A block of shell commands is meant to be typed as it stands, from the root of the repository.
- The pictures are taken by the engine itself, from a running game, not drawn.
- The pages of the engine's own contracts live next to the code, in the `CLAUDE.md` files. They are written for coding agents, and they are the place to look up an exact rule.

## Pages

1. [Run the demo](run-the-demo.md): build once, run `BagiDemo`, open the editor, change a field, save, and see what changed on disk.
2. [Your first scene](first-scene.md): write a scene file by hand, a background, a title and a sprite, and see it in the editor.
3. [A UI screen](ui-screen.md): a settings screen in HTML and CSS on the engine's own canvas, with strings and themes, checked on a phone-shaped window.
4. [A prefab and its overrides](prefab.md): write a node once as a node file, use it three times with their own changes, and see Save write back only what differs.
5. [A tween and a timeline](tween-and-timeline.md): make three nodes move from data, change the curve and the delay, then read the C++ that starts a run and a timeline that fades, moves and calls back.
6. [Config and reflection](config-and-reflection.md): follow one field from its C++ class to the file, the inspector and the control API, write a config file of your own, and change one while the game runs.
7. [Input actions](input-actions.md): make a click send a named action and an emitter listen for it with no C++, then read the C++ that listens and press the pad from a Python script.
8. [A save](save-game.md): give a game a state class, a schema and a slot on disk, read it back into a temporary, and see what a refused file does.
9. [The editor](editor.md): what each panel is for, one small task across four of them, the hotkeys, what a change does to the files, and how the editor differs in a browser.

Planned, not written yet: the web build, the control API from Python, a test, working with agents, the C++ rules explained, where the data lives.

## Where to start

**You make scenes and screens (a designer, an artist).** Start with "Run the demo", then "Your first scene", "A UI screen", "A prefab and its overrides", "A tween and a timeline", "Config and reflection", "Input actions" and "The editor". Planned next: the web build.

**You write Python (tools, bots, tests).** "Run the demo" shows the game you will drive. Planned next: the control API from Python, a test, and where the data lives.

**You work with coding agents.** "Run the demo", "Your first scene", "A UI screen", "A prefab and its overrides", "A tween and a timeline", "Config and reflection", "Input actions", "A save" and "The editor" show what an agent changes when it edits content or adds a component. Planned next: working with agents, and the C++ rules explained.

## A game of your own

The engine knows no game. A game is a repository of its own that carries the engine as a submodule: see [NEW_GAME.md](../../NEW_GAME.md). The pages here use the engine's own demo, `BagiDemo`, so nothing needs a game repository.
