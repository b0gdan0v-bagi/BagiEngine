---
stage: 3
id: no-animation-no-tweens-easing-or-timelines
---
# No animation: no tweens, easing or timelines

**Where:** `games/Match3/CLAUDE.md` ("`Match3GameModel` never animates"), `games/Match3/src/Match3/Components/Match3BoardComponent.cpp`, `src/Modules/TaskSystem/` (coroutines that animation could use).

**What is missing** (found by reading the code, not by running a game): no way to change a property over time. Match3 resolves a whole cascade instantly, and Chess pieces jump to the target square. The frame time it needs exists now (`Time`, `BECore/CLAUDE.md`).

**A fix has to cover:**

- a tween API for float, vector and color properties with easing functions, cancel and completion callbacks, ticked by `dt`;
- use from coroutines (`co_await` a tween) through the TaskSystem on the main thread;
- Match3 as the first consumer (swap, fall and refill animation) while the model stays animation-free (the rule in `games/Match3/CLAUDE.md`);
- gtests for the easing math and for tween ordering with a fake clock.
