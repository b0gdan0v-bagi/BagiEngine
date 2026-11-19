# Art generation: broken cut-outs are found only by looking (no automatic checks, no unattended set)

- Version: 0.5.2
- Entry: artgen-autocheck
- Date: 2026-11-18
- Bump: patch

## Added
- The art service checks each cut-out from its alpha channel: it touches the frame edge, falls into several pieces, keeps a halo or background, is too small, off centre or empty. Thresholds are the `checks` of a preset, the verdict is in the provenance and the `image` event, and `artgen.py check <png>` runs the checks on a file. A PNG without alpha is not applicable, never a failure.
- A run and a set job take extra images with the next seeds until enough variants pass, up to a limit (the request keys `minPass` and `extra`, carried by the bridge).
- `ArtGenVerdict` reads the verdict from a provenance file, and the Generate and Set tabs mark each thumbnail with it.
- The `artgen` skill has an autonomous mode for a set: the checks come first, the agent judges by eye only what passed, in a limited number of rounds.
