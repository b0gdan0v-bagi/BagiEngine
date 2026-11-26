---
paths:
  - "src/Modules/Games/src/Games/GameEditorTools*"
  - "src/Widgets/GamePanel/**/*"
  - "tests/BECore/GameEditorTools*"
  - "tests/BECore/GameCommandForm*"
  - "tests/BECore/GameEditorCommands*"
  - "tests/BECore/GameEditorPanels*"
---

# A game's editor commands and debug panels

A game adds cheat commands and debug panels to the editor without the engine knowing it: it overrides `IGame::RegisterEditorTools` and fills the `GameEditorTools` it is given (`Games/GameEditorTools.h`). `GameManager` owns that registry (`GetEditorTools()`), and the editor reads it. All of it, the hook included, exists only with `BE_WITH_EDITOR`.

## What a game registers

- **A command** (`GameToolCommand`): `name`, `title` (empty: the name), `summary`, parameters (`name`, `type`: `String`, `Integer`, `Number`, `Boolean` or `Json`, `required`, `description`) and the callback. The callback has the contract of `IControllableGame::PerformAction`: it writes exactly one value (`Null()` for nothing to say) and returns success, or returns an error and what it wrote is dropped.
- **A panel** (`GameToolPanel`): `id`, `title` (empty: the id) and the draw callback. The registered entry adds `open`, true at first.

## The editor

The command palette lists `<Game>: <title>` for a command (one with parameters opens a form) and `<Game>: Panel <title>` for a panel. A panel is a dockable window in the Editor run mode only; the draw callback is called between its `ImGui::Begin` and `ImGui::End`, only while the window shows, so it draws the content and leaves the ImGui stacks as it found them. The window is kept in layouts by the prefix and the panel id. Details: `src/Widgets/GamePanel/CLAUDE.md`.

## Names

A command is the control method `<prefix>.<name>`, listed by `list_methods` with its parameters, in every run mode of an editor build. The prefix is the game's name in ASCII lower case with every other byte outside `[a-z0-9_]` as `_`; no engine method has a dot, so a command cannot shadow one. Command names, panel ids and parameter names are `[A-Za-z0-9_]+`. A bad or repeated one, a missing callback, a game whose name is empty and a registration while no game is activated are refused with one error line, never an assert.

## Arguments

The engine checks the declared parameters before the callback, for a control request and for `GameEditorTools::Invoke` alike: a missing required one or a wrong type (`null` too, a number that is not finite too) is `invalid_params` naming it; `Json` takes any value; an undeclared key is ignored. So a callback reads a declared required parameter without a second check, with the readers of `ControlParams.h`. A client still decides the values: check their range. `Invoke` leaves one check to its caller, that the callback wrote a complete value.

## Lifetime

- `SwitchTo` clears the registry before the old game's `Stop()` and calls the new game's hook after its `Start()`; `Shutdown` clears it before `Stop()`. The hook runs on every activation and nothing is carried over. A no-op switch and `ResetActive` leave the registry alone.
- A registration is accepted in the hook and later while that game is active; `Start()` is too early. The registry cannot tell who calls, so a game that registers later does it only while it is the active one.
- A callback may switch the game, which removes its own entry while it runs. Whoever calls a callback holds the entry's `IntrusivePtr` for the whole call and keeps no iterator over the lists across it, and nobody calls a callback of an entry that left the registry. `GetVersion()` grows on every change: a reader keeps a copy of the lists and rebuilds it when the number moves.

## Recording

A command is not written into a session recording: the engine cannot know whether it changes the state. A command that does goes through `ControlledReset`, `ControlledAction` or `ControlledSetState` (`Games/GameControl.h`), or a recording does not replay (`games-replay.md`).

## How a game follows

The override is guarded as the declaration is, or a build without the editor does not compile.

```cpp
// MyGame.h
#if BE_WITH_EDITOR
    void RegisterEditorTools(GameEditorTools& tools) override;
#endif

// MyGame.cpp, with <Games/GameEditorTools.h> and <Games/GameControl.h>
#if BE_WITH_EDITOR
    void MyGame::RegisterEditorTools(GameEditorTools& tools) {
        GameToolCommand deal;
        deal.name = "deal";
        deal.title = "Deal a seed";
        deal.params = {{"seed", GameCommandParamType::Integer, true, "The seed of the deal"}};
        deal.run = [this](JsonView arguments, JsonWriter& result) -> std::expected<void, ControlError> {
            if (auto reset = ControlledReset(*AsControllable(), arguments); !reset) {
                return reset;
            }
            result.Null();
            return {};
        };
        tools.AddCommand(std::move(deal));

        GameToolPanel hands;
        hands.id = "hands";
        hands.draw = [this] { DrawHandsPanel(); };
        tools.AddPanel(std::move(hands));
    }
#endif
```

Tests: `GameEditorToolsTest`, and for the editor `GameEditorCommandsTest`, `GameCommandFormTest` and `GameEditorPanelsTest` (all in `tests/BECore/`, over the hook of `FakeControllableGame`).
