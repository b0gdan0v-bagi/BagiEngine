#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelColumn.h>
#include <BECore/Widgets/PanelAssets/PanelElement.h>

namespace BECore {

    /// What `PanelInterpreter::Prepare` makes of an element's class: the walk switches on this and never asks the type again
    CORE_ENUM(PreparedKind, uint8_t, Unknown, Section, Row, Text, Field, Fields, Input, Button, Table, Separator, EmptyState, Slot)

    /// Whether a binding text was empty (no condition, no bound text), did not parse, or parsed
    CORE_ENUM(PreparedBindingState, uint8_t, Empty, Bad, Ok)

    /// A binding text parsed once at `Prepare`, so the frame never parses (parsing allocates)
    struct PreparedBinding {
        PreparedBindingState state = PreparedBindingState::Empty;
        PanelBinding binding;
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
        PreparedBinding visible;
        /// A button's `enableWhen`
        PreparedBinding enableWhen;
        /// The `bind` of a text, field, fields or input, the `source` of a table
        PreparedBinding bind;
        /// The glyph of a button's, an input's or an empty state's `icon`, null when it names none
        const char* glyph = nullptr;
        /// The hint of an input with the glyph of its icon in front of it
        String hint;
        Vector<PreparedElement> children;
        Vector<PreparedColumn> columns;
    };

}  // namespace BECore
