#pragma once

namespace BECore {

    /**
     * @brief Abstract interface for visiting (mutable) reflected fields in-place
     *
     * Mirrors ISerializer/IDeserializer but for live editing (e.g. ImGui inspector).
     * Visit() returns true when the field value was modified by the visitor.
     *
     * Compound types (Color, Rect, ...) use BeginCompound/EndCompound to group
     * their sub-fields, analogous to BeginObject/EndObject in IArchiveBase.
     */
    class IPropertyVisitor {
    public:
        virtual ~IPropertyVisitor() = default;

        virtual bool Visit(eastl::string_view name, bool& value) = 0;
        virtual bool Visit(eastl::string_view name, int8_t& value) = 0;
        virtual bool Visit(eastl::string_view name, uint8_t& value) = 0;
        virtual bool Visit(eastl::string_view name, int16_t& value) = 0;
        virtual bool Visit(eastl::string_view name, uint16_t& value) = 0;
        virtual bool Visit(eastl::string_view name, int32_t& value) = 0;
        virtual bool Visit(eastl::string_view name, uint32_t& value) = 0;
        virtual bool Visit(eastl::string_view name, int64_t& value) = 0;
        virtual bool Visit(eastl::string_view name, uint64_t& value) = 0;
        virtual bool Visit(eastl::string_view name, float& value) = 0;
        virtual bool Visit(eastl::string_view name, double& value) = 0;
        virtual bool Visit(eastl::string_view name, PoolString& value) = 0;
        virtual bool Visit(eastl::string_view name, eastl::string& value) = 0;

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
        virtual bool VisitEnum(eastl::string_view name, const eastl::string_view* enumNames, size_t enumCount, size_t& selectedIndex) {
            return false;
        }

        /**
         * @brief Begin a compound field group (e.g. tree node in ImGui)
         * @return true if the group is open (children should be visited)
         */
        virtual bool BeginCompound(eastl::string_view name) {
            return true;
        }

        /**
         * @brief End the compound field group opened by BeginCompound
         *
         * Only called when BeginCompound returned true.
         */
        virtual void EndCompound() {}

    protected:
        IPropertyVisitor() = default;
        IPropertyVisitor(const IPropertyVisitor&) = default;
        IPropertyVisitor& operator=(const IPropertyVisitor&) = default;
        IPropertyVisitor(IPropertyVisitor&&) = default;
        IPropertyVisitor& operator=(IPropertyVisitor&&) = default;
    };

}  // namespace BECore
