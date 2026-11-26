#include "EditorControls.h"

#include <Widgets/Feedback/UIMotion.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        /// Enough for an icon, a word and an id
        constexpr size_t kLabelSize = 128;

        /// The icon of an empty state, in ImGui units before the global scales
        constexpr float kEmptyStateIconSize = 32.0f;
        /// The text of an empty state wraps at this many ems (font sizes), or at the width that is free when that is less
        constexpr float kEmptyStateTextWidthInEm = 22.0f;

        /// The blends of every button of the editor: a button is a call, so it has nowhere of its own to keep one
        class ButtonHoverBlends : public Singleton<ButtonHoverBlends> {
        public:
            HoverBlendTable table;
            int snapping = 0;  ///< the live ScopedHoverSnap scopes that snap
        };

        /// @p from to @p to by @p share, weighted by their alphas, so a transparent colour lends the blend no tint
        ImVec4 BlendColors(const ImVec4& from, const ImVec4& to, float share) {
            const float alpha = ImLerp(from.w, to.w, share);
            // The ends are the colours themselves: the way through the alphas may round them to a neighbour
            if (share <= 0.0f || alpha <= 0.0f) {
                return from;
            }
            if (share >= 1.0f) {
                return to;
            }
            const auto channel = [&](float start, float end) { return ImLerp(start * from.w, end * to.w, share) / alpha; };
            return ImVec4(channel(from.x, to.x), channel(from.y, to.y), channel(from.z, to.z), alpha);
        }

    }  // namespace

    ScopedAlpha::ScopedAlpha(float share) : _pushed(share < 1.0f) {
        if (_pushed) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * share);
        }
    }

    ScopedAlpha::~ScopedAlpha() {
        if (_pushed) {
            ImGui::PopStyleVar();
        }
    }

    ScopedHoverSnap::ScopedHoverSnap(bool snap) : _snap(snap) {
        if (_snap) {
            ++ButtonHoverBlends::GetInstance().snapping;
        }
    }

    ScopedHoverSnap::~ScopedHoverSnap() {
        if (_snap) {
            --ButtonHoverBlends::GetInstance().snapping;
        }
    }

    bool BlendedButton(const char* label, float width, float height) {
        const ImVec2 size(width, height);
        ButtonHoverBlends& state = ButtonHoverBlends::GetInstance();
        // A button disabled under the pointer would otherwise still show what is left of its blend
        if (state.snapping > 0 || (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0) {
            return ImGui::Button(label, size);
        }
        HoverBlendTable& blends = state.table;
        const ImGuiID id = ImGui::GetID(label);
        const int frame = ImGui::GetFrameCount();
        const ImVec4 shown = BlendColors(ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), EaseOut(blends.Get(id, frame)));
        // In both slots: ImGui picks one by the hover state of this frame, the blend follows it from the next
        ImGui::PushStyleColor(ImGuiCol_Button, shown);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, shown);
        const bool clicked = ImGui::Button(label, size);
        ImGui::PopStyleColor(2);
        // A window that skips its items leaves the item before as the last one
        blends.Advance(id, ImGui::GetItemID() == id && ImGui::IsItemHovered(), ImGui::GetIO().DeltaTime, frame);
        return clicked;
    }

    void ItemTooltip(const char* tooltip) {
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tooltip);
        }
    }

    bool IconButton(const char* icon, const char* tooltip, const char* id) {
        char label[kLabelSize];
        snprintf(label, sizeof(label), "%s##%s", icon, id);
        const float side = ImGui::GetFrameHeight();
        const bool clicked = BlendedButton(label, side, side);
        ItemTooltip(tooltip);
        return clicked;
    }

    bool IconTextButton(const char* icon, const char* text, const char* tooltip, float width) {
        char label[kLabelSize];
        snprintf(label, sizeof(label), "%s %s", icon, text);
        const bool clicked = BlendedButton(label, width);
        ItemTooltip(tooltip);
        return clicked;
    }

    void EmptyState(const char* icon, const char* text) {
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float wrapWidth = eastl::min(availableWidth, ImGui::GetFontSize() * kEmptyStateTextWidthInEm);
        const ImVec2 textSize = ImGui::CalcTextSize(text, nullptr, false, wrapWidth);
        const float left = ImGui::GetCursorPosX();

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
        ImGui::PushFont(nullptr, kEmptyStateIconSize);
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        ImGui::SetCursorPosX(left + (availableWidth - iconSize.x) * 0.5f);
        ImGui::TextUnformatted(icon);
        ImGui::PopFont();

        // The wrap position is the widest line plus a pixel, so the lines break where CalcTextSize broke them
        const float textLeft = left + (availableWidth - textSize.x) * 0.5f;
        ImGui::SetCursorPosX(textLeft);
        ImGui::PushTextWrapPos(textLeft + textSize.x + 1.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

}  // namespace BECore
