#pragma once

#include <BECore/Reflection/IPropertyVisitor.h>
#include <BECore/Types/Types.h>

namespace BECore {

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
     * - `tooltip("...")`: the text shown while the field is hovered, also a disabled one
     * - `readonly`: the field is drawn disabled (`ImGui::BeginDisabled`); a read-only compound can be opened, its children are disabled
     * - `hidden`: the field is not drawn (the dispatch skips it: `ShowsHidden()` is false)
     * - `required`: not used here
     * - `asset(<Kind>)`: the text field takes a content file of that kind dragged onto it (`Widgets/AssetBrowser/AssetDrag.h`) and holds its
     *   virtual path; not while it is disabled
     * A value is constrained when the user edits it, not when it is shown: a value loaded outside the range stays as it is until touched.
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
        ~ImGuiPropertyVisitor() override = default;

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

        int _idCounter = 0;
        /// The fields between BeginField and EndField, outermost first (a compound's children are visited inside its bracket). Inline, since the visitor is made again every frame
        SmallVector<FieldAttributes, kInlineListDepth> _fields;
        /// The open lists, innermost last
        SmallVector<OpenList, kInlineListDepth> _lists;
    };

}  // namespace BECore
