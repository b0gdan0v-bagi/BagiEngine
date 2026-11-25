# Editor: Save leaves out the nodes components make at run time and writes only non-default fields

- Version: 0.24.1
- Entry: editor-save-generated-nodes
- Date: 2026-11-25
- Bump: patch

## Added
- `SceneNode::IsGenerated()` / `SetGenerated()`: true for a node made at run time (`AddChild`, `CreateNode`, `InstantiateNode`), false for one read from a file or made by `DuplicateNode`. Adapt: nothing; code that builds nodes the editor should keep calls `SetGenerated(false)`.

## Changed
- The Scene Editor's Save and `Scene::PrepareForSave()` skip generated nodes with their subtrees, `DuplicateNode` does not copy them, and "Add node" clears the flag; Save writes only the fields that differ from a fresh component's (and a default `Scene`'s), so saved files no longer gain default values or duplicate run-time children. Adapt: nothing; a scene file saved before with run-time children can be cleaned by saving it once in the editor.
