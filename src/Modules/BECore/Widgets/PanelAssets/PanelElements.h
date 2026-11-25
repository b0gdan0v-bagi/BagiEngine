#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <BECore/Widgets/PanelAssets/PanelColumn.h>
#include <BECore/Widgets/PanelAssets/PanelElement.h>

namespace BECore {

    // The kinds of PanelElement. A text that names a field or a path is a binding: `source.path` (`PanelBinding.h`). The format and the
    // meaning of every field: `BECore/Widgets/PanelAssets/CLAUDE.md`.

    CORE_ENUM(PanelTextStyle, uint8_t, Normal, Disabled, Success, Warning, Error)

    CORE_ENUM(PanelCheckLook, uint8_t, Check, Toggle)

    /// Where a `PanelRow` puts its children on the free width: from the start, or centred as one group
    CORE_ENUM(PanelRowAlign, uint8_t, Start, Center)

    /// The fill of a `PanelButton`: the palette's accent or its muted accent (`None`: the plain button)
    CORE_ENUM(PanelButtonFill, uint8_t, None, Accent, Muted)

    /// How a `PanelSection` looks: a collapsing header, or a tree node that indents its children
    CORE_ENUM(PanelSectionLook, uint8_t, Header, Tree)

    /// A collapsing header (or, with `look` `Tree`, a tree node) over its `elements`; `icon` is the glyph before the title. `openBind` is a bool field the open state is written to
    /// whenever it differs from what the field reads; the section never follows the field (a model reads whether the section is open, it does not open it)
    class PanelSection : public PanelElement {
        BE_CLASS(PanelSection)
    public:
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD bool defaultOpen = true;
        BE_REFLECT_FIELD String icon;
        BE_REFLECT_FIELD String openBind;
        BE_REFLECT_FIELD PanelSectionLook look = PanelSectionLook::Header;
    };

    /// Its `elements` on one line; with `align` `Center` the children are centred as one group on the width that is free
    class PanelRow : public PanelElement {
        BE_CLASS(PanelRow)
    public:
        BE_REFLECT_FIELD PanelRowAlign align = PanelRowAlign::Start;
    };

    /// A bordered or plain region with its own scroll, holding its `elements`; `width` and `height` are in em, 0 fills what is left
    class PanelChild : public PanelElement {
        BE_CLASS(PanelChild)
    public:
        BE_REFLECT_FIELD float width = 0.0f;
        BE_REFLECT_FIELD float height = 0.0f;
        BE_REFLECT_FIELD bool border = false;
    };

    /// A tab bar: its `elements` are `PanelTab`s and nothing else, and the loader reports any other child, an empty bar and a tab outside a bar
    class PanelTabs : public PanelElement {
        BE_CLASS(PanelTabs)
    };

    /// One tab of a `PanelTabs` bar; its `elements` are drawn only while it is the selected one. A tab with no `title` has no label; `icon` is the glyph before the title
    class PanelTab : public PanelElement {
        BE_CLASS(PanelTab)
    public:
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD String icon;
    };

    /// A modal window over its `elements`, shown while the condition `open` reads true (required: with none it could never open). Esc in the focused popup closes it and runs the editor
    /// command `closeCommand`; the model must clear what `open` reads, since the popup opens again only on a rising edge. With no `closeCommand` Esc does nothing
    class PanelPopup : public PanelElement {
        BE_CLASS(PanelPopup)
    public:
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD String open;
        BE_REFLECT_FIELD String closeCommand;
    };

    /// A button (`text` after the glyph `icon`, at least one of them) that opens a non-modal popup of its `elements` under it; a click outside or Esc closes it, so no model state
    /// says it is open. `enableWhen` is a condition that disables the button while false. Never inside another dropdown or a `PanelPopup`, and it holds no `PanelPopup`.
    /// `openCommand` is the editor command that runs, with no argument, once on the frame the popup opens (a model fills the popup's fields there)
    class PanelDropdown : public PanelElement {
        BE_CLASS(PanelDropdown)
    public:
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD String icon;
        BE_REFLECT_FIELD String enableWhen;  ///< a condition like `visible`; `enabled` is the factory's own attribute of every element
        BE_REFLECT_FIELD String openCommand;
    };

    /// A menu of a `Bar` asset: `title` is its label. Its `elements` are menus (submenus), `PanelMenuItem`s, `PanelMenuItems`, separators and texts, and the loader reports any other
    /// child. `enableWhen` is a condition that disables the whole menu while false; `openCommand` is the editor command that runs once when the menu opens (a model refreshes what the
    /// menu lists there)
    class PanelMenu : public PanelElement {
        BE_CLASS(PanelMenu)
    public:
        BE_REFLECT_FIELD String title;
        BE_REFLECT_FIELD String enableWhen;  ///< a condition like `visible`; `enabled` is the factory's own attribute of every element
        BE_REFLECT_FIELD String openCommand;
    };

    /// An item of a `PanelMenu` that runs the editor command `command` with `argument` as a literal text (as a button's). The chord of the command is shown beside the label
    class PanelMenuItem : public PanelElement {
        BE_CLASS(PanelMenuItem)
    public:
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD String icon;
        BE_REFLECT_FIELD String command;
        BE_REFLECT_FIELD String argument;
        BE_REFLECT_FIELD String enableWhen;  ///< a condition like `visible`; `enabled` is the factory's own attribute of every element
    };

    /// One item of a `PanelMenu` per row of the list `source`; the row's text (`itemField`, a path relative to a row, empty for a list of scalars) is the label and the argument of `command`
    class PanelMenuItems : public PanelElement {
        BE_CLASS(PanelMenuItems)
    public:
        BE_REFLECT_FIELD String source;
        BE_REFLECT_FIELD String itemField;
        BE_REFLECT_FIELD String command;
        BE_REFLECT_FIELD String icon;
    };

    /// A literal `text` or the text of the field `bind`, after the glyph `icon`; `maxLines` > 0 shows that many lines and scrolls, also sideways (0: not in a scroll region)
    class PanelText : public PanelElement {
        BE_CLASS(PanelText)
    public:
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD String bind;
        BE_REFLECT_FIELD PanelTextStyle style = PanelTextStyle::Normal;
        BE_REFLECT_FIELD bool wrap = false;
        BE_REFLECT_FIELD bool mono = false;
        BE_REFLECT_FIELD uint32_t maxLines = 0;
        BE_REFLECT_FIELD String icon;
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

    /// A text field bound to a `String` field; `width` is in em, 0 fills the line. The field is written at every change. `focus` takes the keyboard focus on the frame the surrounding
    /// popup appears (so it is only for an input inside a popup); `submitCommand` runs when Enter is pressed in it, `commitCommand` when the user lets go of it after an edit; empty: none.
    /// `lines` > 0 makes it a multiline field that many lines high; Enter is a new line there, so it takes no `submitCommand`. `text` is the label after the field; with a `text`, or
    /// with `fill` false, the field takes ImGui's default item width instead of filling the line (`width` still wins when set). While `enableWhen` reads false it is drawn
    /// disabled and writes nothing
    class PanelInput : public PanelElement {
        BE_CLASS(PanelInput)
    public:
        BE_REFLECT_FIELD String bind;
        BE_REFLECT_FIELD String hint;
        BE_REFLECT_FIELD String icon;
        BE_REFLECT_FIELD float width = 0.0f;
        BE_REFLECT_FIELD bool focus = false;
        BE_REFLECT_FIELD String submitCommand;
        BE_REFLECT_FIELD String commitCommand;
        BE_REFLECT_FIELD uint32_t lines = 0;
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD bool fill = true;
        BE_REFLECT_FIELD String enableWhen;  ///< a condition like `visible`; `enabled` is the factory's own attribute of every element
    };

    /// A bool field `bind` as a check box or, as `Toggle`, a button filled with the accent while on; the label is `text`, or the text of `labelBind` when that is set, after the glyph
    /// `icon`; `style` colours it. It writes the field itself and runs no command, and writes nothing while the condition `enableWhen` reads false
    class PanelCheckbox : public PanelElement {
        BE_CLASS(PanelCheckbox)
    public:
        BE_REFLECT_FIELD String bind;
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD String labelBind;
        BE_REFLECT_FIELD PanelCheckLook look = PanelCheckLook::Check;
        BE_REFLECT_FIELD PanelTextStyle style = PanelTextStyle::Normal;
        BE_REFLECT_FIELD String icon;
        BE_REFLECT_FIELD String enableWhen;  ///< a condition like `visible`; `enabled` is the factory's own attribute of every element
    };

    /// A combo over the enum, the index or the `String` field `bind`; the items are the texts of the list `items` (`itemField`, a path relative to a row, empty for a list of scalars)
    /// or, with no `items`, the names of an enum. `separatorField` is a bool path relative to a row of `items`: a row that reads true is a line, never a choice. It writes the field
    /// itself and runs no command, and writes nothing while the condition `enableWhen` reads false; `width` is in em, 0 is ImGui's default item width. Paths of a row of `items`
    /// (only with `items`): `enableField` a bool, a row that reads false is drawn disabled and never picked (unreadable: enabled); `tooltipField` the text of the row's tooltip.
    /// `previewBind` is a text binding shown while closed instead of the chosen row's text when it reads; `text` is the label after the control
    class PanelCombo : public PanelElement {
        BE_CLASS(PanelCombo)
    public:
        BE_REFLECT_FIELD String bind;
        BE_REFLECT_FIELD String items;
        BE_REFLECT_FIELD String itemField;
        BE_REFLECT_FIELD float width = 0.0f;
        BE_REFLECT_FIELD String separatorField;
        BE_REFLECT_FIELD String enableWhen;  ///< a condition like `visible`; `enabled` is the factory's own attribute of every element
        BE_REFLECT_FIELD String enableField;
        BE_REFLECT_FIELD String tooltipField;
        BE_REFLECT_FIELD String previewBind;
        BE_REFLECT_FIELD String text;
    };

    /// A number field `bind` as a drag, or as a slider when `slider` is set (it needs `min` < `max`); `min` equal to `max` (the default) leaves a drag unbounded; `step` is the drag speed,
    /// 0 picks one; `text` is the label after the control. It writes the field itself and runs no command but `commitCommand`; `width` is in em, 0 is ImGui's default item width, which leaves room for
    /// the label. `commitCommand` runs when the user lets go of the control after an edit (a slider is saved on release, not at every drag frame); empty: none. `input` makes it a
    /// typed field instead of a drag (not with `slider`): the field is written, clamped to `min` / `max` when they differ, only when the user confirms or leaves it after an edit.
    /// `stepper` makes it a typed field with - and + buttons instead (not with `slider` or `input`): `step` is the change of a click, `stepFast` of a Ctrl-click (only with
    /// `stepper`), and the field is written, clamped to `min` / `max` when they differ, at every change. Nothing is written while the condition `enableWhen` reads false.
    /// `decimals` (up to `PanelAssetLimits::kMaxNumberDecimals`, ignored for an integer field) and the literal `unit` shown after the value make the printf format the editor
    /// builds itself, so a file never supplies a format
    class PanelNumber : public PanelElement {
        BE_CLASS(PanelNumber)
    public:
        BE_REFLECT_FIELD String bind;
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD float min = 0.0f;
        BE_REFLECT_FIELD float max = 0.0f;
        BE_REFLECT_FIELD float step = 0.0f;
        BE_REFLECT_FIELD bool slider = false;
        BE_REFLECT_FIELD float width = 0.0f;
        BE_REFLECT_FIELD String commitCommand;
        BE_REFLECT_FIELD bool input = false;
        BE_REFLECT_FIELD String enableWhen;  ///< a condition like `visible`; `enabled` is the factory's own attribute of every element
        BE_REFLECT_FIELD bool stepper = false;
        BE_REFLECT_FIELD float stepFast = 0.0f;
        BE_REFLECT_FIELD String unit;
        BE_REFLECT_FIELD uint32_t decimals = 0;
    };

    /// One choice of a group of radio buttons (a group is several of them, usually in a `PanelRow`): selected while the field `bind` (an enum, an integer or a float) reads equal to
    /// `value`, and a click writes `value` there. `value` is a literal as in a condition: a number, or a bare word that names a value of the enum. The label is `text` after the glyph
    /// `icon`, at least one of them. It runs no command, and writes nothing while the condition `enableWhen` reads false
    class PanelRadio : public PanelElement {
        BE_CLASS(PanelRadio)
    public:
        BE_REFLECT_FIELD String bind;
        BE_REFLECT_FIELD String value;
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD String icon;
        BE_REFLECT_FIELD String enableWhen;  ///< a condition like `visible`; `enabled` is the factory's own attribute of every element
    };

    /// A progress bar: `bind` a number read as a fraction, clamped to 0..1; `textBind` a text binding drawn over the bar; `width` in em, 0 fills the line
    class PanelProgress : public PanelElement {
        BE_CLASS(PanelProgress)
    public:
        BE_REFLECT_FIELD String bind;
        BE_REFLECT_FIELD String textBind;
        BE_REFLECT_FIELD float width = 0.0f;
    };

    /// The `width` of a `PanelButton` that fills the rest of its line (ImGui's own convention for a negative width); any other negative width is a load issue
    inline constexpr float kPanelFillLineWidth = -1.0f;

    /// Runs the editor command `command` with `argument` as a literal text (a button has no row to read a path from; a column of a table has its own `argument`, a path relative to the row).
    /// `width` is in em, 0 is the label's own width, `kPanelFillLineWidth` the rest of the line. `navigable` false keeps the button out of the keyboard navigation: a dialog that reads Space or Enter
    /// itself (a key capture) must not have them press its focused Cancel. `fill` paints the button with the accent while the condition `fillWhen` reads true (empty: always; it needs a `fill`);
    /// `repeat` makes a held button click again on every frame. A button with an `icon` and no `text` is a square icon button. `closePopup` (only inside a `PanelDropdown`) also closes
    /// the dropdown after a click
    class PanelButton : public PanelElement {
        BE_CLASS(PanelButton)
    public:
        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD String icon;
        BE_REFLECT_FIELD String command;
        BE_REFLECT_FIELD String argument;
        BE_REFLECT_FIELD String enableWhen;  ///< a condition like `visible`; `enabled` is the factory's own attribute of every element
        BE_REFLECT_FIELD float width = 0.0f;
        BE_REFLECT_FIELD bool navigable = true;
        BE_REFLECT_FIELD PanelButtonFill fill = PanelButtonFill::None;
        BE_REFLECT_FIELD String fillWhen;
        BE_REFLECT_FIELD bool repeat = false;
        BE_REFLECT_FIELD bool closePopup = false;
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
