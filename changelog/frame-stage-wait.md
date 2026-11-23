# Profiler: the display wait is a frame stage of its own (FrameStage::Wait)

- Version: 0.12.4
- Entry: frame-stage-wait
- Date: 2026-11-23
- Bump: patch

## Added
- `FrameStage::Wait` (`kFrameStageCount` is 6): the Vulkan backend times the fence wait and the swapchain acquire as this stage, so a frame that does under a millisecond of work no longer looks like an 8..32 ms frame of unknown work; the SDL and web backends keep their wait inside `Present`. The control API's `frames` answer (`stagesMs`) and the overlay line carry it; `Vulkan::BeginCommands` is a zone of its own and the log names the present mode and the image count. Adapt: a game that iterates `FrameStage` or indexes `stageMs` by `kFrameStageCount` sees one more stage.

## Changed
- The frame plot draws Wait as a pale segment of the stage split, has a "Work only" switch and shows the mean work and wait. Adapt: nothing.
