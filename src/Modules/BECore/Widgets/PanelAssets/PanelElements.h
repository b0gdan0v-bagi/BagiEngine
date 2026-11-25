#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <BECore/Widgets/PanelAssets/PanelColumn.h>
#include <BECore/Widgets/PanelAssets/PanelElement.h>

namespace BECore {

    // The kinds of PanelElement. A text that names a field or a path is a binding: `source.path` (`PanelBinding.h`). The format and the
    // meaning of every field: `BECore/Widgets/PanelAssets/CLAUDE.md`.

    CORE_ENUM(PanelTextStyle, uint8_t, Normal, Disabled, Success, Warning, Error)

    /// A collapsing header over its `elements`
    class PanelSection : public PanelElement {
        BE_CLASS(PanelSection)
    public:
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD bool defaultOpen = true;
    };

    /// Its `elements` on one line
    class PanelRow : public PanelElement {
        BE_CLASS(PanelRow)
    };

    /// A literal `text` or the text of the field `bind`; `maxLines` > 0 shows that many lines and scrolls, also sideways (0: not in a scroll region)
    class PanelText : public PanelElement {
        BE_CLASS(PanelText)
    public:
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD String bind;
        BE_REFLECT_FIELD PanelTextStyle style = PanelTextStyle::Normal;
        BE_REFLECT_FIELD bool wrap = false;
        BE_REFLECT_FIELD bool mono = false;
        BE_REFLECT_FIELD uint32_t maxLines = 0;
    };

    /// One top-level field of the object `bind` names, drawn as the inspector draws it
    class PanelField : public PanelElement {
        BE_CLASS(PanelField)
    public:
        BE_REFLECT_FIELD String bind;
        BE_REFLECT_FIELD String label;
    };

    /// Every field of the object `bind` names: `source.path`, or a `source` alone for the whole object
    class PanelFields : public PanelElement {
        BE_CLASS(PanelFields)
    public:
        BE_REFLECT_FIELD String bind;
    };

    /// A text field bound to a `String` field; `width` is in em, 0 fills the line
    class PanelInput : public PanelElement {
        BE_CLASS(PanelInput)
    public:
        BE_REFLECT_FIELD String bind;
        BE_REFLECT_FIELD String hint;
        BE_REFLECT_FIELD String icon;
        BE_REFLECT_FIELD float width = 0.0f;
    };

    /// Runs the editor command `command`; `argument` is a path relative to the row of a table it sits in, or empty
    class PanelButton : public PanelElement {
        BE_CLASS(PanelButton)
    public:
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD String icon;
        BE_REFLECT_FIELD String command;
        BE_REFLECT_FIELD String argument;
        BE_REFLECT_FIELD String enableWhen;  ///< a condition like `visible`; `enabled` is the factory's own attribute of every element
    };

    /// The rows of the list `source` names, one cell per column; `maxRows` 0 shows every row; `scrollRows` > 0 shows that many rows and scrolls (0: no scroll region)
    class PanelTable : public PanelElement {
        BE_CLASS(PanelTable)
    public:
        BE_REFLECT_FIELD String source;
        BE_REFLECT_FIELD Vector<PanelColumn> columns;
        BE_REFLECT_FIELD bool header = true;
        BE_REFLECT_FIELD bool borders = false;
        BE_REFLECT_FIELD uint32_t maxRows = 0;
        BE_REFLECT_FIELD uint32_t scrollRows = 0;
    };

    class PanelSeparator : public PanelElement {
        BE_CLASS(PanelSeparator)
    public:
        BE_REFLECT_FIELD String title;
    };

    /// An icon and a line of text for a panel that has nothing to show
    class PanelEmptyState : public PanelElement {
        BE_CLASS(PanelEmptyState)
    public:
        BE_REFLECT_FIELD String icon;
        BE_REFLECT_FIELD String text;
    };

    /// A place for a hand-written drawer registered under `name`
    class PanelSlot : public PanelElement {
        BE_CLASS(PanelSlot)
    public:
        BE_REFLECT_FIELD String name;
    };

}  // namespace BECore
