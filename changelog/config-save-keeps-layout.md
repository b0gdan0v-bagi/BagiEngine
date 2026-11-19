# Config save from the editor drops the comments and reformats the file (TextureLibrary.xml rewritten by one Accept)

- Version: 0.5.0
- Entry: config-save-keeps-layout
- Date: 2026-11-18
- Bump: minor

## Added
- `MergeXmlLayout` (`BECore/Config/XmlLayoutMerge.h`) lays the text of an edited XML document out like the file it was read from.

## Changed
- `XmlDocument::SaveToFile` keeps the layout of the file on disk: the lines an edit did not touch keep their bytes, and a file whose text would not change is not written. A file that cannot carry the layout (quote style, `<a/>` forms, wrapped attributes, a carriage return) is rewritten once in the canonical form, which later saves keep.
- `XmlDocument` writes the attributes of an element on one line, and keeps the comments and the XML declaration of the file.
- `TextureLibrary::Save` replaces `<sprites>` in place, so an Accept in the Generate panel changes only the lines of the sprites.
- `XmlSerializer` writes a float as its shortest text that reads back equal (`116.67`, not `116.669998`).
- `XmlNode::Children()` walks the element children only.

## Breaking
- A config DOM now holds comment and declaration nodes, and `XmlDocument::GetRoot()` returns the root element. Code that walks `pugi` nodes of a config with `first_child()`, `next_sibling()` or `children()` can meet a comment. Adapt: skip the nodes that are not elements, or walk with `XmlNode::Children()`.
