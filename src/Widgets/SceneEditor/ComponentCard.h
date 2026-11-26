#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <cstdint>

namespace BECore {

    class ComponentDefaults;
    class IComponent;
    class TouchHighlights;

    /// What the card of a component belongs to; every part must outlive the draw
    struct ComponentCardContext {
        ComponentDefaults& defaults;
        TouchHighlights& highlights;
        /// The runtime id of the node, the key of the marks
        uint64_t nodeId = 0;
    };

    /// What the user did with a card in this frame
    struct ComponentCardAnswer {
        /// A field changed: the body said so, or "Reset" of the menu changed one
        bool changed = false;
        /// "Remove" was chosen; the caller removes the component, the card changes no node
        bool remove = false;
    };

    /**
     * @brief Draws the card of one component: a framed header (the category stripe, the class icon and name, the flash of a touched component, a "..." menu) and,
     * while it is open, what @p body draws
     *
     * The header's item label is `<glyph> <type name>##card` and the menu button's ends with `##card_menu` (`tools/qa/bagi_qa_editor.py` reads both). The menu has
     * "Reset" (every differing field, `ComponentDefaults::ResetAll`, disabled while none can be reset; the component's marks are dropped), "Copy" (the element a
     * scene file would hold, to the engine's clipboard, with a toast that says whether it went) and "Remove". Needs a frame in progress; under `BeginDisabled` the
     * whole card is disabled. Allocates nothing in a frame with no click.
     *
     * @param index Tells the cards of one window apart: the ImGui ids of the card and of its body are under it
     * @param body Draws the fields; returns true when it changed one
     */
    ComponentCardAnswer DrawComponentCard(int index, IComponent& component, const ComponentCardContext& context, FunctionRef<bool()> body);

}  // namespace BECore
