# Reflection generator: a changed header in a scan folder is generated again

- Version: 0.15.1
- Entry: meta-generator-scan-dirs-stale
- Date: 2026-11-23
- Bump: patch

## Changed
- `CI/meta_generator` parses a header of a `scan_dirs` folder (the engine's `Games`, `Vulkan` and `src/Widgets`) again when its content changed, as it does for `source_dirs`. Before, such a header was parsed only once, so after a change its `.gen.hpp` stayed old and the build failed to link or used stale reflection. Adapt: nothing; a build folder that already failed this way is fixed by one `cmake --preset` after updating the engine (or by deleting `build/<preset>/.meta_cache/`).
