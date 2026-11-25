#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore {

    CORE_ENUM(PanelColumnType, uint8_t, Text, Button, Selectable)

    /**
     * @brief A column of a `PanelTable`
     *
     * `field` and `argument` are property paths relative to a row (the element of the table's list): `field` is what a `Text` or
     * `Selectable` cell shows, `argument` is the value a `Button` hands to its command.
     */
    struct PanelColumn {
        BE_CLASS(PanelColumn)

        BE_REFLECT_FIELD String id;
        BE_REFLECT_FIELD PanelColumnType type = PanelColumnType::Text;
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD String field;
        BE_REFLECT_FIELD String command;
        BE_REFLECT_FIELD String argument;
        BE_REFLECT_FIELD String icon;
        BE_REFLECT_FIELD String tooltip;
    };

}  // namespace BECore
