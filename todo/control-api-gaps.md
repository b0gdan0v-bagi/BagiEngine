---
status: open since 2026-11-01. The control API is done and tested on macOS: the command queue, the game interface with the Match3 and Chess adapters, the WebSocket server, the manual clock with `step`, headless mode with `screenshot`, the MCP server and the Stockfish bridge (`BECore/Control/CLAUDE.md`, `RemoteControl/CLAUDE.md`, `tools/control/CLAUDE.md`). Measured there: Chess against Stockfish 19 to a mate with every position cross-checked, and 11,274 positions of random games with no difference in the legal moves. What is left is below.
stage: 2
---
# Control API: checked on macOS only, no capture on Vulkan, Stockfish games run by hand

**Where:** `src/Modules/RemoteControl/` (Winsock code path), `src/Modules/SDL/` (the `dummy` driver and the software renderer), `src/Modules/Vulkan/src/CoreVulkan/VulkanRendererBackend.cpp` (`RequestCapture`), `tools/control/`, `src/Modules/TaskSystem/src/TaskSystem/TaskScheduler.cpp` (`ScheduleDelayed`).

**What is left:**

1. Windows is unchecked: Winsock and `ws2_32`, the `WinMain` arguments, the `dummy` video driver with the software renderer, and the Python tools (`game_process.py`; the tests that make a fake executable with a shebang are skipped there) have not been built or run on it.
2. The Vulkan backend cannot be seen or run without a window: `--headless` and `screenshot` work on the SDL software renderer only, and `VulkanRendererBackend::RequestCapture` answers with an error (MoltenVK has no headless surface on macOS). Whether that gap is worth closing depends on the decision about the renderer backends.
3. The Stockfish games run by hand: `tools/tests/test_stockfish.py` of BagiEngine-Chess plays real games when Stockfish and a built `BagiChess` exist and skips otherwise, and pytest is not part of ctest, so nothing runs it on a push.
4. Delayed tasks use the wall clock: `TaskScheduler::ScheduleDelayed` computes `Clock::now() + delay`, so `co_await Delay(...)` and `TaskManager::RunDelayed` do not follow `Time::Advance`. A timer or an animation built on them would run in real time under `--manual-clock` and break the determinism the manual clock is for. Nothing in a game uses them yet (only `TaskSystemTest`).
5. A person playing against Stockfish at the window was not tried (the bridge waits for the person's move by polling `get_state`).

**A fix has to cover:**

- the `RemoteControl` tests, the capture tests, the smoke tests and `tools/control/tests` built and run on Windows;
- a capture on the Vulkan backend, or the decision that the SDL software path is the only one to test on;
- the Stockfish games as a ctest entry or a CI step (see [the CI entry](no-ci.md));
- delayed tasks driven by game time (feed the scheduler from `Time`, or add a game-time delay beside the wall-clock one) before the animation work uses them.
