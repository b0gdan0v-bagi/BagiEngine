# Roadmap

Direction for the engine, not a problem entry. The engine has strong infrastructure (reflection, layered config, TaskSystem, ImGui editor, tests) and no runtime for a real 2D game: no animation, text, audio or input mapping. The entries in this folder follow this order. Every "missing" statement was found by reading the code, not by running a game.

1. **Hygiene:** CI is deferred by the project owner ([No CI](no-ci.md); the Linux job in [the test-gaps entry](test-gaps-platforms.md) waits for it). The link from this file to a task tracker and a dashboard is done: `tools/todo_sync/`.
2. **External control API (done on macOS):** a model or a program plays a game over MCP / WebSocket (`tools/control/CLAUDE.md`, `src/Modules/RemoteControl/CLAUDE.md`); what is left (Windows, a capture on Vulkan, the Stockfish games in CI, the clock of delayed tasks) is [the control API entry](control-api-gaps.md).
3. **Minimum for a 2D game, in this order:** animation, 2D rendering, text, audio, input mapping, in-game UI (and the choice of how its screens are authored, [UI authoring](ui-authoring.md)).
4. **Owner decisions:** renderer backends (the Android and web targets in [the release entry](release-builds.md) weigh in; [Renderer backends](renderer-backends.md), [ECS](ecs-nominal.md)), what ECS means in this engine.
5. **Content and quality:** prefabs, saves / hot-reload / packing, test gaps and platforms, the reflection generator.
6. **Third game and releases:** Mahjong, then release builds for PC, Android and web.

**Not now:** networking for players (servers, multiplayer; the control API in stage 2 is a local development tool, not this), 3D, a physics engine of our own (take Box2D when physics is needed), a scripting language, and a rewrite to pure ECS (reflection, the inspector and serialization are built around `IComponent`).

**Driver:** the third game is Mahjong solitaire ([Third game](mahjong.md)), so that stage 3 has a concrete consumer: animation (a pair leaves the board), text (timer, score), audio (a match sound), input mapping (mouse and touch) and in-game UI (new game, shuffle, hint). It does not need a camera, movement with `dt` or a gamepad, so those wait for a game that needs them. Make Match3 animated as the regression check. Stages 1 to 3 are done when tiles and gems animate, the score and the timer are drawn with an engine font, a match plays a sound, input goes through an input map, and CI builds and tests it on macOS and Windows (CI is deferred, so that last point waits for [its entry](no-ci.md)).
