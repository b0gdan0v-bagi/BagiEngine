# Editor: UI validator findings, rects, anchors and safe area are not shown over the viewport

- Version: 0.1.11
- Entry: editor-ui-debug-overlay
- Date: 2026-11-16
- Bump: patch

## Added
- Editor UI debug overlay in the viewport (off by default, switches kept in the editor settings): element rects, anchors and pivots, the design rectangle, the UI validator's findings in place, and a hover tooltip with the style source (`UIVariants::Sources`).
- The `UI Findings` window lists the findings; a click selects the element in the scene editor.
