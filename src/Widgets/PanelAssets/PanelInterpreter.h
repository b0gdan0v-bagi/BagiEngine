#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/PanelAssets/PanelAsset.h>
#include <BECore/Widgets/PanelAssets/PanelBindingReader.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <BECore/Widgets/PanelAssets/SingleFieldVisitor.h>
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
     * A condition that cannot be read counts as true (`visible`, `enableWhen`): a broken binding must be seen, not hide the element. `PanelField::label`
     * is not drawn, the inspector draws the field's own name; `PanelFields` is a `PanelField` whose object or list node starts open (an object is reached as a field of its source: `game.fields`);
     * `PanelInput` and a scrolling table are not drawn yet (a muted `?` / every row).
     *
     * Main thread only; needs an ImGui frame in progress.
     */
    class PanelInterpreter {
    public:
        PanelInterpreter();

        PanelInterpreter(const PanelInterpreter&) = delete;
        PanelInterpreter& operator=(const PanelInterpreter&) = delete;

        /// Holds @p asset and prepares its tree; null draws nothing. Call again only when the asset changed (it allocates)
        void Prepare(IntrusivePtr<PanelAsset> asset);

        /// Draws the prepared asset. @p slots is where a `PanelSlot` finds its drawer (null: the global `PanelSlots`, when it exists). Clears @p result first
        void Draw(const PanelDataSources& sources, PanelDrawResult& result, PanelSlots* slots = nullptr);

        [[nodiscard]] bool IsPrepared() const {
            return static_cast<bool>(_asset);
        }

    private:
        void PrepareElements(const Vector<IntrusivePtr<PanelElement>>& elements, Vector<PreparedElement>& out);

        void DrawElements(const Vector<PreparedElement>& elements);
        void DrawElement(const PreparedElement& prepared);
        void DrawRow(const PreparedElement& prepared);
        void DrawTextLine(const PreparedElement& prepared);
        void DrawButton(const PreparedElement& prepared);
        void DrawField(const PreparedElement& prepared, bool openCompound);
        void DrawTable(const PreparedElement& prepared);
        void DrawCell(const PreparedColumn& prepared, const PanelBinding& list, size_t row);
        void DrawSlot(const PreparedElement& prepared);

        [[nodiscard]] bool IsVisible(const PreparedElement& prepared);
        [[nodiscard]] bool ReadCondition(const PreparedBinding& condition);
        /// The text of the cell @p path of a row as a command's argument; false when it names a path that cannot be read
        [[nodiscard]] bool ReadArgument(const PanelBinding& list, size_t row, StringView path, StringView& out);
        void NoteClick(StringView command, StringView argument);

        IntrusivePtr<PanelAsset> _asset;
        Vector<PreparedElement> _elements;
        PanelBindingReader _reader;
        /// Capacity survives frames: the text of a bound value, of a cell's style and of a selectable's label
        String _scratch;
        String _styleScratch;
        String _label;
        ImGuiPropertyVisitor _inspector;
        SingleFieldVisitor _single;
        /// Valid only inside `Draw`
        const PanelDataSources* _sources = nullptr;
        PanelDrawResult* _result = nullptr;
        PanelSlots* _slots = nullptr;
    };

}  // namespace BECore
