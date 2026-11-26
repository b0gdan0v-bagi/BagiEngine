#include "ComponentCard.h"

#include <BECore/Clipboard/Clipboard.h>
#include <BECore/Config/StringXmlWriter.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/SceneNodeInstance.h>
#include <Widgets/EditorLayout/ComponentCategories.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/EditorLayout/IconTable.h>
#include <Widgets/Feedback/ToastEvents.h>
#include <Widgets/Feedback/TouchFlashDraw.h>
#include <Widgets/Feedback/TouchHighlights.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/ComponentDefaults.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr size_t kHeaderLabelSize = 128;
        constexpr float kStripeWidth = 3.0f;
        /// The room between the last item of an open card and its frame
        constexpr float kBodyPadding = 3.0f;
        constexpr const char* kMenuPopup = "##card_actions";
        constexpr const char* kEnabledTooltip = "Component enabled";
        /// The serialized name of the flag the checkbox writes: what OnPropertyWritten() and the touch marks know it by
        constexpr StringView kEnabledField = "enabled";
        constexpr StringView kCopiedText = "Component copied";
        constexpr StringView kCopyFailedText = "The component was not copied: the clipboard refused it";

        void CopyToClipboard(const IComponent& component) {
            pugi::xml_document document;
            AppendComponentElement(document, component, NodeWriteOptions{.skipDefaults = true});
            StringXmlWriter writer;
            document.first_child().print(writer, "  ");
            while (!writer.text.empty() && writer.text.back() == '\n') {
                writer.text.pop_back();
            }
            if (CoreManager::GetClipboard().SetText(StringView(writer.text.data(), writer.text.size()))) {
                EditorToastEvent::Emit(ToastLevel::Success, kCopiedText);
            } else {
                EditorToastEvent::Emit(ToastLevel::Error, kCopyFailedText);
            }
        }

        bool CanReset(ComponentDefaults& defaults, IComponent& component) {
            for (const ComponentFieldDifference& difference : defaults.Compare(component)) {
                if (difference.resettable) {
                    return true;
                }
            }
            return false;
        }

        void DrawEnabledBox(IComponent& component, const ComponentCardContext& context, ComponentCardAnswer& answer) {
            bool enabled = component.IsEnabled();
            if (ImGui::Checkbox("##card_enabled", &enabled)) {
                component.SetEnabled(enabled);
                component.OnPropertyWritten(kEnabledField);
                context.highlights.Dismiss(context.nodeId, component.GetTypeMeta().typeName, kEnabledField);
                answer.changed = true;
            }
            ItemTooltip(kEnabledTooltip);
        }

        void DrawMenu(IComponent& component, const ComponentCardContext& context, ComponentCardAnswer& answer) {
            if (!ImGui::BeginPopup(kMenuPopup)) {
                return;
            }
            if (ImGui::MenuItem(ICON_LC_ROTATE_CCW " Reset", nullptr, false, CanReset(context.defaults, component))) {
                answer.changed |= context.defaults.ResetAll(component);
                context.highlights.DismissComponent(context.nodeId, component.GetTypeMeta().typeName);
            }
            if (ImGui::MenuItem(ICON_LC_COPY " Copy")) {
                CopyToClipboard(component);
            }
            if (ImGui::MenuItem(ICON_LC_TRASH_2 " Remove")) {
                answer.remove = true;
            }
            ImGui::EndPopup();
        }

    }  // namespace

    ComponentCardAnswer DrawComponentCard(int index, IComponent& component, const ComponentCardContext& context, FunctionRef<bool()> body) {
        ComponentCardAnswer answer;
        const ClassMeta& meta = component.GetTypeMeta();
        const ImGuiPalette& palette = GetImGuiPalette();
        ImGui::PushID(index);

        char label[kHeaderLabelSize];
        snprintf(label, sizeof(label), "%s %.*s##card", IconOfClass(meta), static_cast<int>(meta.typeName.size()), meta.typeName.data());
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        const bool dimmed = !component.IsEnabled();
        if (dimmed) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        }
        // The checkbox and the menu button lie over the header, which must let them take the pointer
        const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_AllowOverlap);
        if (dimmed) {
            ImGui::PopStyleColor();
        }
        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();
        FillItemWithTouchFlash(context.highlights.ComponentAlpha(context.nodeId, meta.typeName));

        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        const Optional<Color> category = CategoryColor(meta);
        const ImU32 stripe = category ? IM_COL32(category->r, category->g, category->b, category->a) : ImGui::GetColorU32(palette.border);
        const float rounding = ImGui::GetStyle().FrameRounding;
        drawList->AddRectFilled(headerMin, {headerMin.x + kStripeWidth, headerMax.y}, stripe, rounding, ImDrawFlags_RoundCornersLeft);

        const float menuLeft = right - ImGui::GetFrameHeight();
        if (component.CanBeDisabled()) {
            ImGui::SameLine();
            ImGui::SetCursorScreenPos({menuLeft - ImGui::GetStyle().ItemInnerSpacing.x - ImGui::GetFrameHeight(), headerMin.y});
            DrawEnabledBox(component, context, answer);
        }
        ImGui::SameLine();
        ImGui::SetCursorScreenPos({menuLeft, headerMin.y});
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (IconButton(ICON_LC_ELLIPSIS_VERTICAL, "Component actions", "card_menu")) {
            ImGui::OpenPopup(kMenuPopup);
        }
        ImGui::PopStyleColor();
        // Before the body, so the fields of this frame show what the menu did
        DrawMenu(component, context, answer);

        float bottom = headerMax.y;
        if (open) {
            ImGui::Indent();
            answer.changed |= body();
            ImGui::Unindent();
            bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y + kBodyPadding;
        }
        drawList->AddRect(headerMin, {headerMax.x, bottom}, ImGui::GetColorU32(palette.border), rounding);

        ImGui::PopID();
        return answer;
    }

}  // namespace BECore
