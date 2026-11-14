#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/FieldAttributes.h>
#include <BECore/Types/Types.h>

namespace BECore {

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
     * edits every field as before, except the ones marked `hidden` (see `ShowsHidden`).
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
         * @brief Visit a reflected enum field with a combo selector
         * @param name Field name
         * @param enumNames Array of enum value names
         * @param enumCount Number of enum values
         * @param selectedIndex Mutable reference to current selection index (0 to enumCount-1)
         * @return true if the selection was changed
         *
         * Default implementation returns false (no-op). Override in subclasses to provide
         * UI (e.g. ImGui::BeginCombo) for selecting enum values.
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
         * @brief Whether the visitor wants the fields marked `hidden` too
         *
         * `false` by default: a `hidden` field gets no `BeginField`, no `Visit` and no `EndField`. A visitor that lists
         * every field (a dump, a diff) returns true.
         */
        virtual bool ShowsHidden() const {
            return false;
        }

    protected:
        IPropertyVisitor() = default;
        IPropertyVisitor(const IPropertyVisitor&) = default;
        IPropertyVisitor& operator=(const IPropertyVisitor&) = default;
        IPropertyVisitor(IPropertyVisitor&&) = default;
        IPropertyVisitor& operator=(IPropertyVisitor&&) = default;
    };

}  // namespace BECore
