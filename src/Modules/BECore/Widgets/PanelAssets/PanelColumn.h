#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore {

    CORE_ENUM(PanelColumnType, uint8_t, Text, Button, Selectable, Input, Checkbox)

    /**
     * @brief A column of a `PanelTable`
     *
     * `field` and `argument` are property paths relative to a row (the element of the table's list): `field` is what a `Text` or
     * `Selectable` cell shows, `argument` is the value a `Button` hands to its command. `styleField` names a field of the enum
     * `PanelTextStyle` of the row: it gives the style of the cell's text per row (empty: the normal style).
     *
     * An `Input` cell is a text field on the `String` at `field` of the row, with `hint` as its hint. A `Button` cell with a `field` is
     * labelled with that text (after its `icon`). A `Checkbox` cell is a check box on the bool at `field` of the row, labelled with the
     * text at `labelField` (only this type may name it). `enableField` is a bool path relative to the row: a `Button`, `Input` or
     * `Checkbox` cell is disabled while it reads false (empty: always enabled). `selectedField` is a bool path relative to the row: a
     * `Selectable` cell is drawn selected while it reads true (empty: never selected); no other column type may name it.
     * `tooltipField` is a text path relative to the row whose text, when it reads, replaces `tooltip` for that row's cell.
     */
    struct PanelColumn {
        BE_CLASS(PanelColumn)

        BE_REFLECT_FIELD String id;
        BE_REFLECT_FIELD PanelColumnType type = PanelColumnType::Text;
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD String field;
        BE_REFLECT_FIELD String styleField;
        BE_REFLECT_FIELD String enableField;
        BE_REFLECT_FIELD String selectedField;
        BE_REFLECT_FIELD String labelField;
        BE_REFLECT_FIELD String hint;
        BE_REFLECT_FIELD String command;
        BE_REFLECT_FIELD String argument;
        BE_REFLECT_FIELD String icon;
        BE_REFLECT_FIELD String tooltip;
        BE_REFLECT_FIELD String tooltipField;
    };

}  // namespace BECore
