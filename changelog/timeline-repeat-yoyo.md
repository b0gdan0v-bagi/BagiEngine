# Animation: a timeline cannot repeat forever or play back and forth (yoyo): no idle loops, pulses or shimmer

- Version: 0.1.1
- Entry: timeline-repeat-yoyo
- Date: 2026-11-15
- Bump: patch

## Added
- `Timeline::Yoyo()`: every second iteration is the exact time reversal of a forward one (clips, delays, callbacks and sub-timelines mirrored).
- `Timeline::RepeatForever()` / `IsEndless()`: an endless timeline; `TweenManager::Play` of one runs until it is cancelled (no `onComplete`, `GetProgress()` stays 0).
- `Timeline::Advance(dt)`: evaluation by the time since the last call, exact for runs of any length.
- `AnimationFrameClass` (`Interactive`, `FiniteEffect`, `DecorativeLoop`), `Timeline::SetFrameClass` / `GetFrameClass`, `TweenHandle::GetFrameClass` and `TweenManager::GetActiveCount(class)`: what keeps the frame loop awake.
