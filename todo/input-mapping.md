---
status: in progress since 2026-11-01. Stage 2 of the plan (restored events, window metrics, `UIInput`) is done; the action map and the gamepad stay open.
stage: 3
plan: magical-leaping-blossom.md
id: input-has-no-mapping-layer-actions-are-just-names
---
# Input has no mapping layer: actions are just names

**Where:** `src/Modules/BECore/Input/ActionsLibrary.h` (a set of action names), `src/Modules/Events/src/Events/InputEvents.h` (`ActionEvent`), `src/Modules/SDL/src/CoreSDL/SDLEventsProvider.cpp` (`ConvertSDLEvent`), `SDLEvents.h` (key, mouse and window event types, no gamepad events), `Scene/Components/ClickableComponent.cpp`.

**What is missing** (found by reading the code, not by running a game):

1. There is no binding from physical input (key, mouse button, gamepad button or axis) to an action. `ActionsLibrary` only lists names, `ClickableComponent` emits the action itself, and keys and gamepads have no path to `ActionEvent`.

**A fix has to cover:**

- an `InputMap` read from XML config (layered, so a game ships its own): key / button / axis → action, with pressed, held and released states and an axis value;
- gamepad support through SDL3 events;
- components that react to mapped actions or raw input subscribe with `SubscribeInScene`, so only the active scene reacts (`ClickableComponent` already does);
- a gtest that feeds raw events and checks the resulting actions.
