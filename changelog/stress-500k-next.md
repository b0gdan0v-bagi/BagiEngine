# BagiStress past 500k: where the frame goes and loads like a game

- Version: 0.29.1
- Entry: stress-500k-next
- Date: 2026-11-26
- Bump: patch

## Added
- `TweenManager::Loop`: a tween that runs turn after turn in its slot without a restart or an allocation (`TweenRunMode`). Adapt: a looping tween built as an `onComplete` restart may move to `Loop`.
- `FrameStats::gpuMs` (Vulkan timestamp queries, `VulkanGpuTimer`; 0 when there is no value), `FrameStatsCollector::SetGpuTime` and the stage `FrameStage::Submit` (the backend's batch submissions). Adapt: take a mean of `gpuMs` over the frames that have one.
- BagiStress: stage times and `gpuMs` in its lines, `--stress-budget-ms`, a cap of 4000000 objects, `--stress-world`, `--stress-churn`, `--stress-mix`, sprite size, overdraw, churn and mixed-field benchmarks, `stress-sprites-vulkan-500k`. Adapt: nothing.

## Changed
- `SceneDrawSystem` culls quads and sprites wholly outside the output: they are no longer sent to the renderer, so the draw counts of a scene with off-screen objects drop. Adapt: nothing; a component that draws itself is not culled.
