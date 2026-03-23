#pragma once

#include <BECore/Reflection/IPropertyVisitor.h>

namespace BECore {

    /**
     * @brief IPropertyVisitor implementation that renders ImGui editing widgets
     *
     * Each Visit() call renders the appropriate ImGui widget for the field type
     * and returns true if the value was modified.
     *
     * Compound types (BeginCompound/EndCompound) are rendered as collapsible
     * tree nodes using ImGui::TreeNode / ImGui::TreePop.
     *
     * Unique ImGui IDs are generated via an internal counter to avoid conflicts
     * when multiple fields share the same display name.
     */
    class ImGuiPropertyVisitor : public IPropertyVisitor {
    public:
        ImGuiPropertyVisitor() = default;
        ~ImGuiPropertyVisitor() override = default;

        bool Visit(eastl::string_view name, bool& value) override;
        bool Visit(eastl::string_view name, int8_t& value) override;
        bool Visit(eastl::string_view name, uint8_t& value) override;
        bool Visit(eastl::string_view name, int16_t& value) override;
        bool Visit(eastl::string_view name, uint16_t& value) override;
        bool Visit(eastl::string_view name, int32_t& value) override;
        bool Visit(eastl::string_view name, uint32_t& value) override;
        bool Visit(eastl::string_view name, int64_t& value) override;
        bool Visit(eastl::string_view name, uint64_t& value) override;
        bool Visit(eastl::string_view name, float& value) override;
        bool Visit(eastl::string_view name, double& value) override;
        bool Visit(eastl::string_view name, PoolString& value) override;
        bool Visit(eastl::string_view name, eastl::string& value) override;

        bool VisitEnum(eastl::string_view name,
                       const eastl::string_view* enumNames,
                       size_t enumCount,
                       size_t& selectedIndex) override;

        bool BeginCompound(eastl::string_view name) override;
        void EndCompound() override;

    private:
        int _idCounter = 0;
    };

}  // namespace BECore
