# Art generation: the cut-out checks pass several objects that lean together (no neck between them)

- Version: 0.6.1
- Entry: artgen-checks-leaning-objects
- Date: 2026-11-19
- Bump: patch

## Added
- The `bodies` check has a second signal, `floor` (how deep the valley between two bodies of the silhouette goes; threshold `minFloor` in the `checks` of a preset): objects that lean together with a shallow neck fail. A group with no valley at all (an object lying across the bottom of the others) still passes.
