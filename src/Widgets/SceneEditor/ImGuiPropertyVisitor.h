#pragma once

#include <BECore/Reflection/IPropertyVisitor.h>
#include <BECore/Types/Types.h>

namespace BECore {

    struct ComponentFieldDifference;
    struct ComponentFieldHelp;
    struct ComponentTouches;

    /**
     * @brief IPropertyVisitor implementation that renders ImGui editing widgets
     *
     * Every Visit returns true when the user changed the value; a compound is a tree node. Every widget gets its ImGui id
     * from a counter, so fields of the same name do not clash.
     *
     * The attributes of `BE_FIELD_ATTRS(...)` arrive through BeginField / EndField (the decisions are in
     * `FieldPresentation`, which needs no ImGui):
     * - `range(min, max)`: a number is a slider; an edit is clamped to the range
     * - `step(s)`: an edit is snapped to multiples of `s` (from `min` with a range); a drag with no range moves by `s` per pixel
     * - `tooltip("...")`: the text shown while the field is hovered, also a disabled one. With a ComponentFieldHelp a top-level field shows the help's
     *   tooltip instead, which holds that text when the help table has none
     * - `readonly`: the field is drawn disabled (`ImGui::BeginDisabled`); a read-only compound can be opened, its children are disabled
     * - `hidden`: the field is not drawn (the dispatch skips it: `ShowsHidden()` is false)
     * - `required`: not used here
     * - `asset(<Kind>)`: the text field takes a content file of that kind dragged onto it (`Widgets/AssetBrowser/AssetDrag.h`) and holds its
     *   virtual path; not while it is disabled
     * A value is constrained when the user edits it, not when it is shown: a value loaded outside the range stays as it is until touched.
     *
     * With a ComponentTouches, a top-level field that a client of the control API changed is marked once it is drawn: its flash over it and the line
     * "changed: <client>" under it (a change inside a compound or a list marks the top-level field). An edit of the field removes the mark.
     *
     * A top-level field named by one of the differences from the default gets DrawDefaultMark beside it once it is drawn; a click of its arrow changes
     * nothing during the walk and is answered by `ResetRequest()`. A field that draws nothing has no mark.
     *
     * Containers (the hooks of `IPropertyVisitor`; the dispatch applies what they ask, after the list, never here):
     * - a sequence or a map is a tree node `name (size)`, an `x` button after each element and a `+` after the last; a
     *   button the `ListInfo` does not allow is drawn disabled. A map with text keys has a key field before its `+`, which
     *   is disabled while the field is empty; an integer key needs no field (the dispatch takes the first unused one)
     * - an `Optional` has a checkbox in front of its value (its name alone while it holds none)
     * - a `Variant` has a combo of its alternatives by index, then the active one under `value`
     * - a `Value<T>` field says what it is in a dim line (`Slot: caster.attack`); when `ValueContextRegistry` has the provider `inspector`
     *   it also has a node `Explain` with the value now and the trace of how it came to be. The explanation is made only while that node is open
     * The elements are visited inside the container field's `BeginField`, so `range` and `step` reach its numeric elements
     * through `CurrentField`, and `readonly` disables the elements, the buttons, the checkbox and the combo (the node can
     * still be opened).
     */
    class ImGuiPropertyVisitor : public IPropertyVisitor {
    public:
        ImGuiPropertyVisitor() = default;
        /// Marks the fields of the component @p touches names (null: none) and the fields @p differences names, and gives the top-level fields the
        /// tooltips of @p help (null: their attribute's); all must outlive the visit
        explicit ImGuiPropertyVisitor(const ComponentTouches* touches, Span<const ComponentFieldDifference> differences = {}, const ComponentFieldHelp* help = nullptr)
            : _touches(touches), _help(help), _differences(differences) {}
        ~ImGuiPropertyVisitor() override = default;

        /// The serialized name of the field whose reset arrow was clicked in this walk, in static storage; empty when none was
        [[nodiscard]] StringView ResetRequest() const {
            return _resetRequest;
        }

        bool Visit(StringView name, bool& value) override;
        bool Visit(StringView name, int8_t& value) override;
        bool Visit(StringView name, uint8_t& value) override;
        bool Visit(StringView name, int16_t& value) override;
        bool Visit(StringView name, uint16_t& value) override;
        bool Visit(StringView name, int32_t& value) override;
        bool Visit(StringView name, uint32_t& value) override;
        bool Visit(StringView name, int64_t& value) override;
        bool Visit(StringView name, uint64_t& value) override;
        bool Visit(StringView name, float& value) override;
        bool Visit(StringView name, double& value) override;
        bool Visit(StringView name, PoolString& value) override;
        bool Visit(StringView name, String& value) override;

        bool VisitEnum(StringView name, const StringView* enumNames, size_t enumCount, size_t& selectedIndex) override;

        bool BeginCompound(StringView name) override;
        void EndCompound() override;

        void BeginField(const FieldAttributes& attributes) override;
        void EndField() override;

        bool BeginList(StringView name, const ListInfo& info) override;
        void EndListElement(size_t index, ListRequest& request) override;
        void EndList(ListRequest& request) override;
        void VisitNewKey(String& key) override;
        bool VisitPresence(StringView name, bool& present) override;
        bool VisitAlternative(StringView name, size_t count, size_t& index) override;
        void VisitValueField(StringView name, const ValueFieldInfo& info) override;

    private:
        /// What an open list allows and what its key field asked, between BeginList and EndList
        struct OpenList {
            bool canAdd = false;
            bool canRemove = false;
            /// VisitNewKey drew the `+` beside the key field, so EndList draws none
            bool addDrawn = false;
            bool addClicked = false;
        };

        /// Lists and compounds nest this deep with no allocation (the visitor is made again every frame)
        static constexpr size_t kInlineListDepth = 8;

        /// The attributes of the field being visited: those of the innermost open BeginField, `FieldAttributes{}` outside one
        const FieldAttributes& CurrentField() const;

        /// The id of the next widget, one of the field or element @p name; a top-level field takes its name from the first of these
        int NextId(StringView name);
        /// @p changed, remembered for the field that is open
        bool Edited(bool changed);
        /// The help's tooltip of the open top-level field; null with no help, inside a nested field and before a widget named the field
        [[nodiscard]] const char* HelpText() const;
        /// The tooltip of the open field for the last item: the help's, else its attribute's
        void FieldTooltip(const FieldAttributes& attributes) const;
        /// A reflected number: a slider with a `range`, a drag otherwise (`DrawNumber`)
        template <typename T>
        bool VisitNumber(StringView name, T& value, int dataType);
        /// The mark and the flash of the top-level field that was just drawn
        void ShowTouch() const;
        /// The default mark of the top-level field that was just drawn, when it differs
        void ShowDifference(float bottom);

        int _idCounter = 0;
        const ComponentTouches* _touches = nullptr;
        const ComponentFieldHelp* _help = nullptr;
        Span<const ComponentFieldDifference> _differences;
        StringView _resetRequest;
        StringView _fieldName;    ///< of the open top-level field; empty until a widget of it names it
        float _fieldLeft = 0.0f;  ///< the screen position the open top-level field began at
        float _fieldTop = 0.0f;
        float _fieldWidth = 0.0f;
        bool _fieldEdited = false;  ///< the user changed something of the open top-level field
        /// The fields between BeginField and EndField, outermost first (a compound's children are visited inside its bracket). Inline, since the visitor is made again every frame
        SmallVector<FieldAttributes, kInlineListDepth> _fields;
        /// The open lists, innermost last
        SmallVector<OpenList, kInlineListDepth> _lists;
    };

}  // namespace BECore
