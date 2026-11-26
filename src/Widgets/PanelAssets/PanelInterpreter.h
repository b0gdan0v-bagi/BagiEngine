#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/PanelAssets/PanelAsset.h>
#include <BECore/Widgets/PanelAssets/PanelBindingReader.h>
#include <BECore/Widgets/PanelAssets/PanelBindingWriter.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <BECore/Widgets/PanelAssets/SingleFieldVisitor.h>
#include <Widgets/PanelAssets/PanelDrawSupport.h>
#include <Widgets/PanelAssets/PanelSlots.h>
#include <Widgets/PanelAssets/PreparedPanel.h>
#include <Widgets/SceneEditor/ImGuiPropertyVisitor.h>

namespace BECore {

    /// What a frame of the panel asked for: the click of one button, to be emitted after the walk
    struct PanelDrawResult {
        bool clicked = false;
        String command;
        String argument;

        /// Keeps the capacity of both texts, so a later click in a steady frame allocates nothing
        void Clear() {
            clicked = false;
            command.clear();
            argument.clear();
        }
    };

    /**
     * @brief Draws a `PanelAsset` on ImGui inside the window the caller has begun
     *
     * `Prepare` parses every binding text of the tree once per asset; `Draw` then reads the bound values through its `PanelBindingReader` and
     * draws, and allocates nothing in a steady frame (the texts it reads go into members whose capacity stays). A value that cannot be read is a
     * `?`, never an error: a gone source, a wrong path and a field of another kind are what a hot-reloaded asset shows while it is edited.
     *
     * **A click is noted, not run.** The walk may be over the very list a command changes, so `Draw` only puts the command and the argument of
     * the clicked button or cell into the `PanelDrawResult`; the caller emits `EditorCommandEvent` after the call. At most one click per frame
     * is kept (the last).
     *
     * **Ids** (the control API finds a control by them): `PushID` of the element's `id` around every element, and inside a table the row index
     * and the column `id` around every cell. A button's own label (`<icon> <text>`, `<icon>##<column id>` in a table) is the last link of the chain.
     *
     * A condition (`visible`, `enableWhen`, `open`: `PanelCondition`, parsed once) with a term that cannot be read counts as true, except a popup's, which counts as closed: a broken binding must be
     * seen, not hide the element. `PanelField::label` is not drawn, the inspector draws the field's own name; `PanelFields` is a `PanelField` whose object or list node starts open (an object is
     * reached as a field of its source: `game.fields`), or the whole object when its `bind` is a source alone.
     *
     * **A text input** copies the bound `String` into a buffer of its own and writes it back through `PanelBindingWriter` when ImGui reports a
     * change, as the inspector does; a text longer than the buffer is cut at a character boundary, and an edit keeps only what the buffer holds.
     * Its label is `##<id>` (the element's, or the column's in a table), so the control API finds it by that.
     *
     * **A table with `scrollRows`** scrolls inside a region of that many rows once it has more, and submits only the rows in view (an
     * `ImGuiListClipper`): a row outside the window is not a control, so the control API does not find it. The ids of a cell are the same
     * as in a table that does not scroll.
     *
     * **A child region** (`PanelChild`) is an ImGui child window labelled `##<element id>` under the element's `PushID`. A child is a window of its own, so the ids of the controls in it start from
     * the child's window id, not from the chain of the window around it.
     *
     * **A popup** (`PanelPopup`) is a modal window the model opens with a bool: the walk calls `OpenPopup` when `open` turns true (and the popup is
     * not open in ImGui), and `CloseCurrentPopup` as soon as it reads false (its children are drawn disabled that frame, so nothing can be clicked while it
     * goes). Unlike every other condition, one that cannot be read counts as closed: a modal opened by a broken file would block the editor. A click inside
     * is noted like any other, so the command that clears the bool takes effect on the next frame. The modal opens on the rising edge of `open` only. ImGui never closes a modal on Esc, so
     * a popup with a `closeCommand` does it: Esc in the focused popup closes it and notes the command, so the model clears the bool; the popup does not reopen while the bool still
     * reads true. A popup with no `closeCommand` ignores Esc.
     *
     * **A dropdown** (`PanelDropdown`) is a button that opens a non-modal popup (`##dropdown` under the element's id) under it; ImGui closes it on a click outside or Esc, so
     * no model state says it is open. The popup is a window of its own, so the ids of its controls start from that window's id.
     *
     * **A tab bar** (`PanelTabs`) is `BeginTabBar` labelled `##<id>`; each `PanelTab` is a `BeginTabItem` labelled `<title>###<id>` whose children are drawn only while it is the
     * selected one. ImGui pushes the tab's own id inside it, so a control in a tab has the bar's, the tab's and then its own ids in its chain. A tab outside a bar draws `?`, and a
     * child of a bar that is no tab is not drawn (the loader reports both).
     *
     * **A text input's commands.** `focus` takes the keyboard focus on the frame the window it is in appears. With `submitCommand` the field returns on Enter
     * (`EnterReturnsTrue`) and the command is noted; `commitCommand` is noted when the user lets go of the field or of a number after an edit. Both are clicks: one command per frame,
     * the last, and the submit comes after the commit so it wins.
     *
     * **Menus** (`PanelMenu`, `PanelMenuItem`, `PanelMenuItems`) are `BeginMenu` / `MenuItem` and need a window the caller gave a menu bar (`BeginMenuBar` around `Draw`) or a popup; anywhere else
     * they draw `?`. A menu is `<title>###<id>` and its children are drawn only while it is open; its `openCommand` is noted on the first frame of the opening, not on every open frame (a menu is not
     * drawn while it is closed, so a gap in the frame numbers is the edge). An item is `<icon> <text>###<id>` with the chord of its command (`SetChordText`) right-aligned; `PanelMenuItems` is one
     * item per row of its list under `PushID(row)`, the row's text being the label and the argument. A click is noted like a button's.
     *
     * **A row with `align` `Center`** centres its children as one group on the free width, by the width they took the frame before (a `mutable` float of the prepared row): the first frame draws at
     * the start and a changed width is followed one frame later. **A button** with a `fill` is painted with the palette's accent or muted accent while its `fillWhen` reads true (hover and active as
     * the accent's); `repeat` is `ImGuiItemFlags_ButtonRepeat`, so a held button notes a click on every repeat frame; an `icon` with no `text` is the square `IconButton` labelled `<icon>##<id>`.
     *
     * **A radio** (`PanelRadio`) is `<icon> <text>###<id>`, selected while `bind == value` reads true (a condition of one term, prepared once); a click writes `value`, a word as its index
     * among the enum's names (looked up once), and writes nothing when the enum lacks the name. **A section** is `<icon> <title>###<id>`; with `openBind` the header's open state is written to
     * that bool whenever it differs from what the field reads (a read that fails writes nothing). **A stepper** (`PanelNumber stepper`) is an `InputScalar` with - and + buttons, written
     * and clamped at every change; `step` 0 steps by one. **A multiline input** (`PanelInput lines`) is `lines` text lines high on the same buffer. A button of width
     * `kPanelFillLineWidth` fills the rest of its line.
     *
     * **The editor's host.** A button, a menu item or the items of a list whose command the host does not offer (`EditorHost.h`) are not drawn, whatever their `visible` says; the
     * host is asked once, in `Prepare`.
     *
     * Main thread only; needs an ImGui frame in progress.
     */
    class PanelInterpreter {
    public:
        /// The bytes a text field holds, the terminator included: room for the longest text a panel edits (the Sprite Library's list of names)
        static constexpr size_t kInputBufferSize = 2048;

        PanelInterpreter();

        PanelInterpreter(const PanelInterpreter&) = delete;
        PanelInterpreter& operator=(const PanelInterpreter&) = delete;

        /// Holds @p asset and prepares its tree; null draws nothing. Call again only when the asset changed (it allocates)
        void Prepare(IntrusivePtr<PanelAsset> asset);

        /// Draws the prepared asset. @p slots is where a `PanelSlot` finds its drawer (null: the global `PanelSlots`, when it exists). Clears @p result first
        void Draw(const PanelDataSources& sources, PanelDrawResult& result, PanelSlots* slots = nullptr);

        /// Where the chord of an item's command comes from; null (the default) shows none. Not owned: the caller keeps it alive while it draws
        void SetChordText(PanelChordText* chords) {
            _chords = chords;
        }

        /// Whether @p element is a kind the interpreter really draws (not an unknown class)
        [[nodiscard]] static bool CanDraw(const PanelElement& element);

        [[nodiscard]] bool IsPrepared() const {
            return static_cast<bool>(_asset);
        }

    private:
        void PrepareElements(const Vector<IntrusivePtr<PanelElement>>& elements, Vector<PreparedElement>& out);

        void DrawElements(const Vector<PreparedElement>& elements);
        void DrawElement(const PreparedElement& prepared);
        void DrawSection(const PreparedElement& prepared);
        void DrawRow(const PreparedElement& prepared);
        void DrawTextLine(const PreparedElement& prepared);
        static void DrawLines(bool wrap, StringView shown);
        void DrawButton(const PreparedElement& prepared);
        void DrawInput(const PreparedElement& prepared);
        void DrawCheckbox(const PreparedElement& prepared);
        void DrawRadio(const PreparedElement& prepared);
        /// Writes a radio's `value` into its field: a number as it is, a word as its index among the enum's names (nothing when the enum lacks it)
        void WriteRadioValue(const PreparedElement& prepared);
        void DrawCombo(const PreparedElement& prepared);
        void DrawNumber(const PreparedElement& prepared);
        /// A number as a drag or a slider, written at every change
        void DrawDraggedNumber(const PreparedElement& prepared, double value, bool integer);
        /// A number with `input`: a typed field, written (clamped) and its `commitCommand` noted only when the user confirms or leaves it after an edit
        void DrawTypedNumber(const PreparedElement& prepared, double value, bool integer);
        /// A number with `stepper`: a typed field with - and + buttons, written (clamped) at every change
        void DrawStepperNumber(const PreparedElement& prepared, double value, bool integer);
        /// The combo of an enum with no `items`: its names, the index of the value held
        void DrawEnumCombo(const PanelCombo& combo, const PreparedElement& prepared);
        /// The combo over a list: the value is an index into it or the text of one of its rows
        void DrawListCombo(const PanelCombo& combo, const PreparedElement& prepared);
        void DrawField(const PreparedElement& prepared, bool openCompound);
        void DrawTable(const PreparedElement& prepared);
        void DrawCell(const PreparedColumn& prepared, const PanelBinding& list, size_t row);
        void DrawRows(const PreparedElement& prepared, size_t rows, bool clipped);
        void DrawButtonCell(const PreparedColumn& prepared, const PanelBinding& list, size_t row);
        void DrawInputCell(const PreparedColumn& prepared, const PanelBinding& list, size_t row);
        void DrawCheckboxCell(const PreparedColumn& prepared, const PanelBinding& list, size_t row);
        void DrawSlot(const PreparedElement& prepared);
        void DrawChild(const PreparedElement& prepared);
        void DrawPopup(const PreparedElement& prepared);
        void DrawDropdown(const PreparedElement& prepared);
        void DrawProgress(const PreparedElement& prepared);
        void DrawTabs(const PreparedElement& prepared);
        void DrawTab(const PreparedElement& prepared);
        void DrawMenu(const PreparedElement& prepared);
        void DrawMenuItem(const PreparedElement& prepared);
        void DrawMenuItems(const PreparedElement& prepared);

        [[nodiscard]] bool IsVisible(const PreparedElement& prepared);
        /// @p whenUnreadable is what a condition that is bad or has a term that cannot be read counts as; an empty one is true
        [[nodiscard]] bool ReadCondition(const PreparedCondition& condition, bool whenUnreadable = true);
        /// The text of the cell @p path of a row as a command's argument; false when it names a path that cannot be read
        [[nodiscard]] bool ReadArgument(const PanelBinding& list, size_t row, StringView path, StringView& out);
        /// The bool cell @p path of a row; true when @p path is empty or cannot be read, so a broken binding is seen and not hidden
        [[nodiscard]] bool ReadRowCondition(const PanelBinding& list, size_t row, StringView path);
        /// Whether the bool cell @p path of a row reads true (a selected row, a combo's separator row); false when it is empty or cannot be read
        [[nodiscard]] bool ReadRowFlag(const PanelBinding& list, size_t row, StringView path);
        /// Copies @p current into `_inputBuffer`, cut at a character boundary when it does not fit
        void FillInputBuffer(StringView current);
        /// Shows @p current in a text field under @p label, @p widthPixels wide (0: ImGui's default item width); true when the user changed it (with @p enterOnly: pressed
        /// Enter), the new text being in `_inputBuffer`
        [[nodiscard]] bool DrawTextBox(const char* label, StringView current, const char* hint, float widthPixels, bool enterOnly = false);
        /// The tooltip of the last item, while it is hovered (also disabled): the text of the element's `tooltipBind` when it reads, else its `tooltip`
        void ElementTooltip(const PreparedElement& prepared);
        /// The tooltip of the last item drawn for a row of @p list (a table's cell, a combo's row): the text at @p path of the row when it reads, else @p literal
        void RowTooltip(StringView literal, const PanelBinding& list, size_t row, StringView path);
        /// What a combo shows closed: the text of its `previewBind` when it reads, else @p chosen
        [[nodiscard]] const char* ComboPreview(const PreparedElement& prepared, const String& chosen);
        void NoteClick(StringView command, StringView argument);
        /// The chord text of @p command in `_chordScratch`, null when there is none or no provider
        [[nodiscard]] const char* ChordOf(StringView command);

        IntrusivePtr<PanelAsset> _asset;
        Vector<PreparedElement> _elements;
        PanelBindingReader _reader;
        PanelBindingWriter _writer{_reader};
        /// Capacity survives frames: the text of a bound value, of a cell's style and of a button cell, and of a label
        String _scratch;
        /// The text of a word term; apart from `_scratch`, which a cell or a child may still view while a condition is read
        String _conditionScratch;
        String _styleScratch;
        String _cellScratch;
        String _label;
        /// The chord of the menu item being drawn, copied so `MenuItem` gets a C string whatever the provider returns
        String _chordScratch;
        /// What a combo shows closed; `BeginCombo` wants it as a C string
        String _preview;
        /// A combo's `previewBind` text, copied for `BeginCombo`
        String _boundPreview;
        /// The bound text of the tooltip being shown; apart from the others, which the hovered control may still view
        String _tooltipScratch;
        /// What the text field being drawn edits; one field is drawn at a time
        char _inputBuffer[kInputBufferSize] = {};
        /// The typed number being edited: ImGui reports the value only on the frames the text changed, and it is written when the field is left, so the last value is kept
        /// here under the field's id until then. One field is active at a time
        ImGuiID _typedId = 0;
        double _typedValue = 0.0;
        ImGuiPropertyVisitor _inspector;
        SingleFieldVisitor _single;
        /// Valid only inside `Draw`
        const PanelDataSources* _sources = nullptr;
        PanelDrawResult* _result = nullptr;
        PanelSlots* _slots = nullptr;
        PanelChordText* _chords = nullptr;
        /// Whether the walk is inside a dropdown's popup, the one place a button's `closePopup` closes; a file with issues may put one elsewhere
        bool _inDropdown = false;
    };

}  // namespace BECore
