#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelColumn.h>
#include <BECore/Widgets/PanelAssets/PanelCondition.h>
#include <BECore/Widgets/PanelAssets/PanelElement.h>
#include <Widgets/EditorLayout/LayoutStore.h>

namespace BECore {

    /// What `PanelInterpreter::Prepare` makes of an element's class: the walk switches on this and never asks the type again
    CORE_ENUM(PreparedKind, uint8_t, Unknown, Section, Row, Text, Field, Fields, Input, Checkbox, Combo, Number, Button, Table, Separator, EmptyState, Slot, Child, Popup, Tabs, Tab, Menu, MenuItem,
              MenuItems, Dropdown, Radio, Progress)

    /// Whether a binding text was empty (no condition, no bound text), did not parse, or parsed
    CORE_ENUM(PreparedBindingState, uint8_t, Empty, Bad, Ok)

    /// A binding text parsed once at `Prepare`, so the frame never parses (parsing allocates)
    struct PreparedBinding {
        PreparedBindingState state = PreparedBindingState::Empty;
        PanelBinding binding;
    };

    /// A condition text parsed once at `Prepare`; `Bad` and `Empty` hold no terms and are told apart because `open` treats an empty one as closed
    struct PreparedCondition {
        PreparedBindingState state = PreparedBindingState::Empty;
        PanelCondition condition;
    };

    struct PreparedColumn {
        /// Owned by the asset the interpreter holds
        const PanelColumn* column = nullptr;
        /// The glyph of `column->icon`, null when it names none
        const char* glyph = nullptr;
        /// The hint of an `Input` cell with the glyph of the icon in front of it
        String hint;
    };

    /// One element of the asset with what a frame needs already resolved. `element` and every view into it belong to the asset the interpreter holds
    struct PreparedElement {
        const PanelElement* element = nullptr;
        PreparedKind kind = PreparedKind::Unknown;
        /// False for a button or a menu item whose command the editor's host does not offer (`IsEditorCommandAvailable`): it is never drawn
        bool offered = true;
        PreparedCondition visible;
        /// The `enableWhen` of a button, a dropdown, a menu, a menu item, an input, a checkbox, a combo, a number or a radio
        PreparedCondition enableWhen;
        /// A button's `fillWhen`
        PreparedCondition fillWhen;
        /// A popup's `open`: unlike the others, a condition that cannot be read counts as false
        PreparedCondition open;
        /// A radio's `bind == value`: selected while it reads true; one that cannot be read shows the radio unselected
        PreparedCondition match;
        /// The `bind` of a text, field, fields, input, checkbox, combo, number, radio or progress, the `openBind` of a section, the `source` of a table or of a `PanelMenuItems`
        PreparedBinding bind;
        /// A checkbox's `labelBind`, a combo's `previewBind`, a progress's `textBind`
        PreparedBinding label;
        /// The `tooltipBind` of any element: its text replaces `tooltip` when it reads
        PreparedBinding tooltip;
        /// A combo's `items`
        PreparedBinding items;
        /// The glyph of a button's, a dropdown's, an input's, a checkbox's, a radio's, a section's, a text's, a tab's, a menu item's or an empty state's `icon`, null when it names none
        const char* glyph = nullptr;
        /// The hint of an input with the glyph of its icon in front of it
        String hint;
        /// The printf formats of a number with `decimals` or a `unit`, for a float field and for an integer one; empty (ImGui's default) when it has neither
        String format;
        String integerFormat;
        Vector<PreparedElement> children;
        Vector<PreparedColumn> columns;
        /// A popup's `open` as the last frame read it: the walk is const over the tree and this is the one thing it remembers, so the modal opens on the rising edge
        /// of `open` and a modal closed by Esc while `open` still reads true does not reopen. A new `Prepare` forgets it
        mutable bool popupWasWanted = false;
        /// A menu or a dropdown's popup is drawn only while it is open (and not at all while a parent is closed), so the first open frame after a gap is the opening:
        /// `openCommand` runs then. A new `Prepare` forgets it
        mutable MenuOpenTracker menuOpened;
        /// Where a radio's word `value` is among the names of its enum: looked up at the first click that can read the names, then kept (none: the enum lacks the name,
        /// so a click writes nothing). A new `Prepare` forgets it
        mutable bool radioIndexKnown = false;
        mutable Optional<size_t> radioIndex;
        /// The width the children of a centred row took last frame: zero on the first frame, so the row starts at the left once and is centred from the next frame on
        mutable float rowWidth = 0.0f;
    };

}  // namespace BECore
