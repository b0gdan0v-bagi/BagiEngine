# A text change rebuilds its layout result from scratch: ~4.5 heap allocations per layout, no capacity reuse

- Version: 0.9.6
- Entry: text-layout-reuse
- Date: 2026-11-23
- Bump: patch

## Added
- `TextLayout::Layout(font, text, options, TextLayoutResult& into)` lays out into a result that keeps its capacity; `UIVars::Expand(text, String& out)` and `UIStrings::Resolve(text, out)` write into the caller's buffer.

## Changed
- A UI text whose value changes (a timer, a score, a language switch) is laid out and expanded with no heap allocation once its buffers have grown: the layout cache and `UITextPainter` reuse their results instead of building new ones.
