# Editor: inspectors lose data (texture rename overwrites another entry, flag fields drop unknown tokens, Add reuses one name)

- Version: 0.2.1
- Entry: editor-inspector-data-loss
- Date: 2026-11-16
- Bump: patch

## Added
- `TextureLibrary::Rename(from, to)` (refuses a taken name) and `TextureLibrary::MakeUniqueName(base)`.
- `SDLUtils::GetWindowFlagNames()`: the one list of window flag names the loader reads and the editor offers.

## Changed
- Sprite Library editor: a rename onto a taken name is refused with a warning, Add makes a unique name (`new_sprite_2`, ...), the name field follows the selected entry after Add and Delete.
- Config editor: a flags field keeps tokens that are not among its options (shown checked and marked "(not a known value)"); an unregistered attribute with `|` is shown as a String, not as guessed flags.
- UI inspector: the region-name field is bound to its element, so a name typed for one element is never written to another.
