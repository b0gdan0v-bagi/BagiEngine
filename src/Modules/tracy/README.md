# Tracy client (vendored)

The client part (`public/`) of the Tracy Profiler, https://github.com/wolfpld/tracy, release **v0.15.0** (commit `f2a4f2529e7e71844c9e434b877770a21ecf7587`), BSD 3-clause (`LICENSE`). Unedited; `public/TracyClient.F90` (Fortran) is left out.

Built only with `ENGINE_PROFILER_TRACY=ON` (preset `macos-profile-tracy`): `src/Modules/BECore/CMakeLists.txt` compiles `public/TracyClient.cpp` into `BECoreModule` with `TRACY_ENABLE;TRACY_ON_DEMAND;TRACY_MANUAL_LIFETIME;TRACY_DELAYED_INIT`, without the PCH. The engine reaches it only through `BECore/Profiler/Backends/TracyProfilerBackend.cpp`.

**The viewer must be of the same version** (the protocol changes between releases): build `profiler/` of the same tag, or take the Windows binary attached to the v0.15.0 release on GitHub, or on macOS `brew install tracy` when Homebrew has 0.15.0 (`tracy --version`). Start the game, then connect the viewer to `127.0.0.1` (port 8086).

To update: replace `public/` and `LICENSE` with those of the new tag, change the version here and in `src/Modules/BECore/Profiler/HowToProfile.md`, build `macos-profile-tracy` and run its tests.
