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
              MenuItems)

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
        PreparedCondition visible;
        /// A button's, a menu's or a menu item's `enableWhen`
        PreparedCondition enableWhen;
        /// A popup's `open`: unlike the others, a condition that cannot be read counts as false
        PreparedCondition open;
        /// The `bind` of a text, field, fields, input, checkbox, combo or number, the `source` of a table or of a `PanelMenuItems`
        PreparedBinding bind;
        /// A checkbox's `labelBind`
        PreparedBinding label;
        /// A combo's `items`
        PreparedBinding items;
        /// The glyph of a button's, an input's, a menu item's or an empty state's `icon`, null when it names none
        const char* glyph = nullptr;
        /// The hint of an input with the glyph of its icon in front of it
        String hint;
        Vector<PreparedElement> children;
        Vector<PreparedColumn> columns;
        /// A popup's `open` as the last frame read it: the walk is const over the tree and this is the one thing it remembers, so the modal opens on the rising edge
        /// of `open` and a modal closed by Esc while `open` still reads true does not reopen. A new `Prepare` forgets it
        mutable bool popupWasWanted = false;
        /// A menu is drawn only while it is open (and not at all while a parent is closed), so the first open frame after a gap is the opening: `openCommand` runs then.
        /// A new `Prepare` forgets it
        mutable MenuOpenTracker menuOpened;
    };

}  // namespace BECore
