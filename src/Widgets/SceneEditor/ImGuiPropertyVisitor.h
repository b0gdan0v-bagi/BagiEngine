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

    private:
        int _idCounter = 0;
    };

}  // namespace BECore
