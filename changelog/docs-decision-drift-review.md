# Docs drift from the owner's decisions: no review finds statements that later decisions in the todo store replaced

- Version: 0.1.6
- Entry: docs-decision-drift-review
- Date: 2026-11-16
- Bump: patch

## Changed
- Engine docs (rules, skills, agents, nested `CLAUDE.md`) brought in line with the owner's later decisions: renderer backends, Vulkan status, audio, build output, `BECore` type names.
- `todo-reviewer` checks a diff's docs against the entry's decision paragraphs; `todo-developer`'s docs step names them; the `/todo-merge` report lists the docs and open entries that still state a replaced policy.
