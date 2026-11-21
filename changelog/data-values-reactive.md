# Derived values keep version keys by hand: no rebuild by declared dependencies, a forgotten key is a stale screen or an allocation per frame

- Version: 0.8.4
- Entry: data-values-reactive
- Date: 2026-11-21
- Bump: patch

## Added
- `Derived<T>` (`BECore/Values/Derived/Derived.h`): a cached value rebuilt only when one of its declared reads moved. The reads are a `DerivedReads`: version counters (`AddSource`) and levels of a `Context` (`AddLevel`, `AddLevels`); the builder is passed to `Get`, and a hit allocates nothing. In a build with asserts the builder gets `Context::Restricted(levels)`, so reading an undeclared level fails an assert instead of leaving a stale value.
- `UIAmbient` (`BECore/UI/UIAmbient.h`): the language and the window metrics as the context levels `language` and `window` (`UIAmbient::Declare`, `UIAmbient::Fill`), fed by `UIStrings::GetLanguageView()` and `UICanvasComponent::GetWindowView()` / `GetWindowVersion()`.
- `UIStrings::GetVersionCounter()` and `UIVars::GetVersionCounter()`: the address of the version counter, for `DerivedReads::AddSource` and `Context::Set`.
- `UITextPainter::GetExpandCount()` and `UITextComponent::GetExpandCount()`: how many times the display text was expanded.
- Benchmarks `derived_*` of `ValueBench` in the perf set: the cost of a `Derived` hit.

## Changed
- `UITextPainter` keeps its expanded text as a `Derived<String>`; when a text is expanded again is unchanged (its source, a `UIVars` variable, or the string table for a text with a `{#key}`).
  Adapt: nothing; a game that keeps version keys by hand for a screen value can move it to `Derived` over its `Context` and the `UIAmbient` levels.
