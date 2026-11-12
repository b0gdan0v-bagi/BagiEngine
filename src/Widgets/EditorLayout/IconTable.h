#pragma once


namespace BECore {

    struct ClassMeta;
    class SceneNode;

    /// One Lucide icon: its name (kebab case, no prefix, as `ICON=` in `BE_CLASS` writes it) and the glyph in the UI font
    struct IconEntry {
        eastl::string_view name;
        const char* glyph = nullptr;
    };

    /// The icons the editor and the engine's components use, sorted by name (a static_assert in IconTable.cpp holds it).
    /// To add one: put a row in its place in IconTable.cpp, `{"name", ICON_LC_NAME}` from Icons/IconsLucide.h.
    eastl::span<const IconEntry> GetIconTable();

    /// The glyph of a Lucide name, or null when the table has no such row. Binary search, never allocates.
    const char* FindIcon(eastl::string_view name);

    /// The icon of a node without a component that names one (`box`)
    const char* GetNodeIcon();

    /// The glyph of `meta.icon`; the generic node icon for an empty name, and for a name that is not in the table, which
    /// is logged once per name. Safe to call every frame: nothing is allocated for a name that was seen. Names must
    /// outlive the call (they are literals of the generated `ClassMeta`).
    const char* IconOfClass(const ClassMeta& meta);

    /// The icon of a node's main component: the first component, in the node's order, whose class declares an icon;
    /// the generic node icon when there is none.
    const char* IconOfNode(const SceneNode& node);

}  // namespace BECore
