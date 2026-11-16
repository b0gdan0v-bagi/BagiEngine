#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>

namespace BECore {

    class Scene;
    class SceneNode;
    class UIElement;

    /// How the save reaches the node files, so the unit holds no file system: @p ref is the reference of the node file (`nodes/MahjongHud`). Both return false with the reason in @p error
    struct UINodeFileIo {
        FunctionRef<bool(StringView ref, String& text, String& error)> read;
        FunctionRef<bool(StringView ref, StringView text, String& error)> write;
    };

    /// What a save of the UI edits did: the files written, the elements whose rect is now in a file, and one note per file or element that was left out (what the editor logs)
    struct UINodeFileReport {
        size_t filesWritten = 0;
        size_t elementsPatched = 0;
        Vector<String> notes;
    };

    /**
     * @brief The text of the node file of @p node with the rects of the edited elements of its canvases written in
     *
     * Each element of @p edited that lies in a `UICanvasComponent` of @p node is found in the parsed @p fileText (UIRectFilePatch) and only the rect attributes that
     * differ from the live `style.rect` are edited (XmlTextPatch): every other byte of the file stays. An element that is not in the file as the scene has it (added in
     * code, another `type` or `name`) or a write that XmlTextPatch refuses is one note in @p notes and the others are still patched; a file that does not parse is one note.
     * Elements of other nodes are ignored.
     *
     * @param patched receives the new text, @p fileText when nothing changed
     * @param elementsPatched receives how many elements got an edit
     * @return true when @p patched differs from @p fileText
     */
    bool PatchNodeFileText(StringView fileText, const SceneNode& node, Span<const IntrusivePtr<UIElement>> edited, String& patched, size_t& elementsPatched, Vector<String>& notes);

    /**
     * @brief Writes the rect edits of @p edited into the node files their nodes came from
     *
     * For every node of @p scene with a source reference and a canvas that holds an edited element: the file is read with `io.read`, patched (PatchNodeFileText) and written with
     * `io.write` only when its text changed. A reference that more than one node of the scene uses is skipped with a note that names it and the count (the other instances would
     * change on the next load); so is a file that cannot be read or written, with the reason. Nodes written inline in the scene file are not in the report: the scene file holds them.
     * Never throws and never stops at the first failure.
     */
    UINodeFileReport SaveUIRectsToNodeFiles(Scene& scene, Span<const IntrusivePtr<UIElement>> edited, const UINodeFileIo& io);

}  // namespace BECore
