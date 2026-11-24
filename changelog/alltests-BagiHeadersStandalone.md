# alltests: BagiHeadersStandalone fails

- Version: 0.10.2
- Entry: alltests-BagiHeadersStandalone
- Date: 2026-11-23
- Bump: patch

## Changed
- With `ENGINE_NO_EXCEPTIONS` (the default) the engine defines `FMT_EXCEPTIONS=0` for every target, beside `EASTL_EXCEPTIONS_ENABLED=0`. fmt took `_HAS_EXCEPTIONS` of the MSVC library (1 whatever the flags) for the truth, so with Clang on Windows it used `try`, which `-fno-exceptions` refuses outside a system header: the build compiled only through CMake's PCH wrapper, and `BagiHeadersStandalone` failed for every header on Windows. On macOS, Linux and the web nothing changes (fmt already chose 0 there).
  Adapt: configure again (a definition of every target: a full rebuild). A game target compiled outside `cmake/BagiEngine.cmake` defines `FMT_EXCEPTIONS=0` too.
