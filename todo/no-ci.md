---
status: deferred by the project owner on 2026-10-31. Skip it when taking the next roadmap item; the entry stays as the record of what a fix has to cover.
stage: 1
id: no-ci-builds-and-tests-run-only-on-a-developer-machine
---
# No CI: builds and tests run only on a developer machine

**Where:** the repository has no `.github/` and no other pipeline definition. Presets are in `CMakePresets.json`, tests run through `ctest --preset ...`.

**What is missing** (found by reading the repository, not by running anything):

1. Nothing builds or tests a push. The engine supports macOS and Windows, so a break on the other OS goes unnoticed until someone builds there.
2. The Vulkan loader is vendored only in part: `libvulkan.1.dylib` (macOS) and `vulkan-1.lib` (Windows) are tracked, the `*.dll` files are git-ignored and have to be provided by hand (`external/Vulkan-Loader/README.md`). A CI runner on Windows needs an answer for the runtime DLL, if the test and game executables load it.

**A fix has to cover:**

- a workflow for macOS (`macos-debug`) and Windows (one MSVC or Clang preset) that checks out the submodules (`git submodule update --init --recursive`, `external/googletest` included), configures, builds the game and test targets and runs `ctest`;
- a decision for the Vulkan loader DLL on the Windows runner (install the SDK, fetch the loader, or build without the Vulkan backend), after checking whether the test executables need it at all;
- a guard on the number of tests (669 ctest entries at the last count, from `ctest --preset macos-debug -N`; the pytest suites of `tools/` come on top: 93 in `tools/control/tests`, 49 in `tools/todo_sync/tests`): it must not drop;
- a Linux job once Linux presets exist (see [Test gaps and platforms](test-gaps-platforms.md));
- once the games have their own repositories, the workflow builds the workspace repository (checkout with submodules), not the engine alone (see [Games live in the engine repository](games-to-own-repos.md)).
