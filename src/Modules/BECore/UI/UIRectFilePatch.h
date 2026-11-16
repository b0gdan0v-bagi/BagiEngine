#pragma once

#include <BECore/Config/XmlTextPatch.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIRectSpec.h>
#include <pugixml.hpp>

namespace BECore {

    class UICanvasComponent;
    class UIElement;

    /**
     * @brief From a live canvas element to the attribute edits of its `<element>` in a node file, so the editor can write the rect back as a text patch (XmlTextPatch)
     *
     * Pure: no scene, no file system. Only the rect (the attributes of UIRectSpec) is ever named; `visible`, the stack fields and `fontSize` are changed by game code at run time and
     * stay as the file has them.
     */
    class UIRectFilePatch {
    public:
        UIRectFilePatch() = delete;

        /**
         * @brief The place of @p element among the authored elements of @p canvas: `[i, c1, c2, ...]`, `_elements[i]` and then `children[c1]`, `children[c2]`...
         *
         * Nothing for an element that is not in the authored tree (added in code, the canvas's root).
         */
        [[nodiscard]] static Optional<Vector<uint32_t>> FindAuthoredPath(const UICanvasComponent& canvas, const UIElement& element);

        /**
         * @brief The `<element>` of a node file that stands for the authored element at @p path
         *
         * @param nodeEl the `<node>` of the file
         * @param canvasOrdinal which of the node's `UICanvasComponent`s holds the element (the k-th `<component type="UICanvasComponent">`)
         * @param path the path of FindAuthoredPath: the i-th `<element>` of `<elements>`, then of each `<children>`
         * @param live the element; the file's one must have its `type` and its `name`
         * @param why the reason when the result is empty
         * @return an empty node when the file has no such element or it is another one (the scene file's `<elements>` override may have changed the type or the name)
         */
        [[nodiscard]] static pugi::xml_node FindFileElement(pugi::xml_node nodeEl, uint32_t canvasOrdinal, Span<const uint32_t> path, const UIElement& live, String& why);

        /**
         * @brief The edits that make the rect of @p fileElement, read back, equal @p live
         *
         * A rect attribute is compared by what it parses to, so `x="10"` against 10 or a default written out is no edit; one that differs, or that only @p live has, is set; one that
         * only the file has (the live rect went back to the default) is removed. The order is that of UIElementStyle::Serialize.
         */
        [[nodiscard]] static Vector<XmlTextPatch::AttributeEdit> RectAttributeEdits(pugi::xml_node fileElement, const UIRectSpec& live);
    };

}  // namespace BECore
