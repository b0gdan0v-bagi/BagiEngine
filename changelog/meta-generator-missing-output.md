# Reflection generator: a deleted .gen.hpp is written again, and --force covers every scanned folder

- Version: 0.16.3
- Entry: meta-generator-missing-output
- Date: 2026-11-23
- Bump: patch

## Changed
- `CI/meta_generator` writes a header's `Generated/<Name>.gen.hpp` again when the file is missing, even if the header did not change, so deleting `build/<preset>/Generated/` and running `cmake --preset` restores it. `--force` now also regenerates the headers of the `scan_dirs`-only folders (the engine's `Games`, `Vulkan`, `src/Widgets`), and a header is parsed once per run. Adapt: nothing.
