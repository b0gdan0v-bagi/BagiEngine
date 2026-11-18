# UISliderComponent with partShape=Round allocates in a steady frame

- Version: 0.4.1
- Entry: ui-slider-round-allocates
- Date: 2026-11-18
- Bump: patch

## Changed
- Tests in the profile build hold a slider, a switch and a progress bar with `partShape=Round` to zero allocations in a steady frame. The engine code did not change: the allocations came from the storage of the test renderer `RecordingRenderer`.
