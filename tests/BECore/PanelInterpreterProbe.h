#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>

namespace BECore::Tests {

    /// A row of the list the interpreter tests draw as a table
    struct InterpreterRow {
        BE_CLASS(InterpreterRow)

        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD PanelTextStyle style = PanelTextStyle::Normal;
        BE_REFLECT_FIELD String note;
        BE_REFLECT_FIELD bool enabled = true;
        BE_REFLECT_FIELD bool selected = false;
    };

    /// An object a `PanelFields` shows
    struct InterpreterInner {
        BE_CLASS(InterpreterInner)

        BE_REFLECT_FIELD bool on = false;
    };

    /// The source object of the interpreter tests. `flag` is the first field, so a click on the first control of a `PanelField` over it toggles it
    struct InterpreterProbe {
        BE_CLASS(InterpreterProbe)

        BE_REFLECT_FIELD bool flag = false;
        BE_REFLECT_FIELD bool hidden = false;
        /// The condition of the popups of the tests
        BE_REFLECT_FIELD bool dialog = false;
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD String filter;
        BE_REFLECT_FIELD InterpreterInner inner;
        BE_REFLECT_FIELD Vector<InterpreterRow> rows;
    };

}  // namespace BECore::Tests
