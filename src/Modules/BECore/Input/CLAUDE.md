# Input

`UIInput` (`CoreManager::GetUIInput()`) is the one door for mouse input. `SDLEventsProvider` (`SDL/CLAUDE.md`), `ViewportWidget` (the editor viewport, through `ViewportPointer::Forward`) and the control API's `ui_click` call `DispatchMouseButtonDown/Up`, `DispatchMouseMotion`, `DispatchMouseWheel`; none of them emits the `SDLEvents` mouse events itself. Positions are render output pixels.

## The walk

- Registered `IUIInputHandler`s are asked topmost first: the higher `order`, among equals the later registered. A handler is asked only when its `ContainsPoint(x, y)` holds (default `true`: a canvas decides for itself); the first `true` answer consumes the event. Only an event nobody consumed is emitted raw for game code, so a click on a button does not also hit the board under it. A press nobody consumed starts no capture.
- **The order key is the draw position of the handler's place:** `Scene::GetInputOrder(component)` = node draw rank times `Scene::kMaxComponentsPerNode` plus the component's index on the node. Canvases, RmlUi documents and scene targets all register with it and refresh it in `OnDraw()` (also while hidden) by `Unregister` + `Register` only when it changed, so they cover each other as they are drawn. A game component that listens to raw mouse events is the bottom of the order.
- **A scene target** (`ClickableComponent`; the model for more) is a handler that: registers in `OnAttached`, unregisters in its destructor, stamps `Scene::GetDrawPass()` in `OnDraw()` and keeps `HitClip::FromRenderer()` there, and answers `ContainsPoint` false unless its scene is active, it drew in the current pass (a hidden node or an inactive scene draws nothing, so takes nothing; a pass counter of 0, a test that never draws, counts as drawn) and the point is inside its clip and shape. The rect is the node's unrotated `TransformComponent`. It takes the press, its release and the motion over the rect (so the canvas below shows no hover there and gets the leave), except the motion of a drag of the game (a button held that it did not take); while it holds its press it takes every motion. **Game code therefore gets no raw down, up or motion over a clickable's rect**, only the `ActionEvent`; a game that needs the pointer there listens to its action or moves the clickable.
- A handler answers `true` only for what it used. `OnPointerLeave(which)` is called once when the pointer it was over (the last motion it consumed) goes to another handler or to nothing.

## Capture and hover

`PointerTracker` keeps, per pointer (`which` of the event) and button, the handler that consumed a press, and per pointer the hover. The up of a held press goes to its holder first, wherever the pointer is and whatever `ContainsPoint` says, and is consumed whatever the holder answers (a release never leaks to the game or to a handler that did not see the press); the motion of a pointer that holds a press goes to the holder first, and the walk goes on without it when it answers `false`. A handler therefore needs no pressed state of its own for the release (a canvas keeps its own for its elements; the capture is the second line). `Unregister` forgets the handler's captures and hover at once, so a handler that re-registers to change its order loses a held press: do it only when the order moved.

## Rules

- Register and unregister inside a handler are safe: a registration joins after the dispatch in progress, an unregistered handler is not called again, not even by that dispatch, and is never called to leave. A handler that unregisters itself while answering `true` starts no capture.
- **A dispatch allocates nothing:** no container per event; `PointerTracker` holds `kMaxEntries` captures and hovers inline (a further one is refused and the dispatcher then simply holds nothing).
- Tests: `PointerTrackerTest`, `UIInputTest`, `UIInputPointerTest`, `SceneInputOrderTest`, `ClickableComponentTest`.
