#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/FieldAttributes.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore {

    /// What a container field lets its visitor do (IPropertyVisitor::BeginList)
    struct ListInfo {
        size_t size = 0;
        /// An element can be appended: a resizable container with room, or a map whose key the visitor can supply (text or integer)
        bool canAdd = false;
        /// An element can be removed: a resizable container or a map that is not empty
        bool canRemove = false;
    };

    CORE_ENUM(ListAction, uint8_t, None, Add, Remove)

    /**
     * @brief What a visitor asks of a container during its visit
     *
     * Advisory: the dispatch applies it once, after `EndList` (never while the elements are visited, so no element moves under
     * a visit), and drops it when the `ListInfo` of the list did not allow it. Add appends a default element (a map: a default
     * value under the key of `VisitNewKey`); Remove takes out the element at `index`, its position in the visit.
     */
    struct ListRequest {
        ListAction action = ListAction::None;
        size_t index = 0;
    };

    /**
     * @brief Abstract interface for visiting (mutable) reflected fields in-place
     *
     * Mirrors ISerializer/IDeserializer but for live editing (e.g. ImGui inspector).
     * Visit() returns true when the field value was modified by the visitor.
     *
     * Compound types (Color, Rect, ...) use BeginCompound/EndCompound to group
     * their sub-fields, analogous to BeginObject/EndObject in IArchiveBase.
     *
     * Every reflected field is visited between `BeginField(attributes)` and `EndField()` (see `PropVisit::VisitField`): the
     * brackets nest around a compound's `BeginCompound` / `EndCompound` and the visits of its children, and `EndField`
     * always follows its `BeginField`. All three field hooks have a default, so a visitor that overrides none of them
     * edits every field except the ones marked `hidden` (see `ShowsHidden`).
     *
     * A container field is visited inside its own `BeginField` / `EndField` too, so its elements see the field's attributes
     * (`range` and `step` mean its numeric elements). The container hooks (`BeginList` ... `EndList`, `VisitNewKey`,
     * `VisitPresence`, `VisitAlternative`) all have defaults: a visitor that overrides none sees a sequence or a map as a
     * compound of its elements, cannot add, remove, engage or switch anything, and edits the elements. A `hidden` field
     * reaches none of the hooks.
     */
    class IPropertyVisitor {
    public:
        virtual ~IPropertyVisitor() = default;

        virtual bool Visit(StringView name, bool& value) = 0;
        virtual bool Visit(StringView name, int8_t& value) = 0;
        virtual bool Visit(StringView name, uint8_t& value) = 0;
        virtual bool Visit(StringView name, int16_t& value) = 0;
        virtual bool Visit(StringView name, uint16_t& value) = 0;
        virtual bool Visit(StringView name, int32_t& value) = 0;
        virtual bool Visit(StringView name, uint32_t& value) = 0;
        virtual bool Visit(StringView name, int64_t& value) = 0;
        virtual bool Visit(StringView name, uint64_t& value) = 0;
        virtual bool Visit(StringView name, float& value) = 0;
        virtual bool Visit(StringView name, double& value) = 0;
        virtual bool Visit(StringView name, PoolString& value) = 0;
        virtual bool Visit(StringView name, String& value) = 0;

        /**
         * @brief Visit a reflected enum field; @p selectedIndex is in [0, enumCount)
         * @return true if the selection was changed; the default changes nothing
         */
        virtual bool VisitEnum(StringView name, const StringView* enumNames, size_t enumCount, size_t& selectedIndex) {
            return false;
        }

        /**
         * @brief Begin a compound field group (e.g. tree node in ImGui)
         * @return true if the group is open (children should be visited)
         */
        virtual bool BeginCompound(StringView name) {
            return true;
        }

        /**
         * @brief End the compound field group opened by BeginCompound
         *
         * Only called when BeginCompound returned true.
         */
        virtual void EndCompound() {}

        /**
         * @brief A reflected field is about to be visited
         * @param attributes What `BE_FIELD_ATTRS(...)` says about the field (`FieldAttributes{}` when it has none); valid until `EndField`
         *
         * A visitor that draws or checks the field reads them here and applies them to the `Visit` / `VisitEnum` /
         * `BeginCompound` calls that follow.
         */
        virtual void BeginField(const FieldAttributes& attributes) {}

        /// @brief The field opened by `BeginField` has been visited (after its compound's `EndCompound`, when it is one)
        virtual void EndField() {}

        /**
         * @brief A sequence (`Vector`, `SmallVector`, `FixedVector`, `Array`) or a map (`HashMap`, `FixedHashMap`, `Map`) begins
         * @return true when its elements are to be visited; the default opens a compound, `BeginCompound(name)`
         *
         * Then each element is visited under its label (`[i]` in a sequence; in a map the text of a string, integer or enum key,
         * `[i]` for another key) and followed by `EndListElement`; a map that allows an add gets `VisitNewKey` after the last
         * element; `EndList` closes the list, only when this returned true.
         */
        virtual bool BeginList(StringView name, const ListInfo& info) {
            return BeginCompound(name);
        }

        /// @brief Element @p index has been visited; a visitor that removes it writes the request here
        virtual void EndListElement(size_t index, ListRequest& request) {}

        /// @brief The list opened by BeginList ends (the default closes the compound, `EndCompound()`); the request is applied after this call
        virtual void EndList(ListRequest& request) {
            EndCompound();
        }

        /**
         * @brief The key an add to a map would use, for a map with text keys; called before `EndList` when the list allows an add
         * @param key Comes in empty; what it holds when an `Add` request is applied is the key of the new entry
         *
         * A key the map already has is not added. The default leaves the key empty.
         */
        virtual void VisitNewKey(String& key) {}

        /// @brief The same for a map with integer keys: @p key comes in as the smallest non-negative integer that is no key yet
        virtual void VisitNewKey(int64_t& key) {}

        /**
         * @brief An `Optional` field: whether it holds a value, before the value is visited (under the same name, when it holds one)
         * @return true when the visitor changed @p present: the dispatch then engages a default value or resets the optional
         */
        virtual bool VisitPresence(StringView name, bool& present) {
            return false;
        }

        /**
         * @brief A `Variant` field: which alternative is active, inside its compound and before the active alternative is visited
         *        under `value`
         * @param count How many alternatives the variant has; @p index is the active one
         * @return true when the visitor set another index: the dispatch emplaces a default value of that alternative
         */
        virtual bool VisitAlternative(StringView name, size_t count, size_t& index) {
            return false;
        }

        /**
         * @brief Whether the visitor wants the fields marked `hidden` too
         *
         * `false` by default: a `hidden` field gets no `BeginField`, no `Visit` and no `EndField`. A visitor that lists
         * every field (a dump, a diff) returns true.
         */
        virtual bool ShowsHidden() const {
            return false;
        }

        /**
         * @brief The String a text field that is not a `String` (a FixedString, another allocator) is edited through, for the dispatch (`VisitText` of PropertyVisitorDispatch.h)
         *
         * One per visitor, so its capacity is kept from field to field: a text that fits the inline buffer never allocates and a longer one allocates once, not once per field. Nothing else
         * uses it, and a text field has no children, so no visit of one is open while another starts.
         */
        String& TextScratch() {
            return _textScratch;
        }

    protected:
        IPropertyVisitor() = default;
        IPropertyVisitor(const IPropertyVisitor&) = default;
        IPropertyVisitor& operator=(const IPropertyVisitor&) = default;
        IPropertyVisitor(IPropertyVisitor&&) = default;
        IPropertyVisitor& operator=(IPropertyVisitor&&) = default;

    private:
        String _textScratch;
    };

}  // namespace BECore
