# Tweens: the running state of every tween is walked from a dense array

- Version: 0.21.1
- Entry: tween-dense-storage
- Date: 2026-11-25
- Bump: patch

## Changed
- `TweenManager` keeps the state a running tween needs each frame by value in one array and finds it by the handle's id; `Advance` reads no handle and touches no reference count until a tween ends. The public API of `Tween.h` and `TweenManager.h` and the rules of `Animation/CLAUDE.md` are unchanged. Adapt: nothing.
