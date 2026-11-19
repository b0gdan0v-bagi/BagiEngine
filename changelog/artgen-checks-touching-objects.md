# Art generation: the cut-out checks pass several touching objects as one piece

- Version: 0.4.7
- Entry: artgen-checks-touching-objects
- Date: 2026-11-19
- Bump: patch

## Added
- The cut-out check `bodies`: the `valley` metric of the silhouette fails a cut-out of several objects joined by a thin neck (thresholds `maxValley` and `minBodyWidth` in the `checks` of a preset), and the Generate and Set tabs name it in the verdict. Objects that lean together with no neck still pass (entry `artgen-checks-leaning-objects`).
