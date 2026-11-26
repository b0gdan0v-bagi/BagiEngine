#include "ToastOverlay.h"

#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        constexpr const char* kWindowName = "##Toasts";
        constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                                  ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove;
        /// Sizes in ems (font sizes), so a card follows the font scale
        constexpr float kCardWidthInEm = 24.0f;
        constexpr float kMarginInEm = 0.75f;
        constexpr size_t kCountTextSize = 16;

        /// The icon of a level and the colour of it and of the card's border
        struct LevelLook {
            const char* icon = nullptr;
            ImVec4 color;
        };

        LevelLook LookOf(ToastLevel level, const ImGuiPalette& palette) {
            switch (level) {
                case ToastLevel::Info:
                    return {ICON_LC_INFO, palette.accent};
                case ToastLevel::Success:
                    return {ICON_LC_CIRCLE_CHECK, palette.success};
                case ToastLevel::Warning:
                    return {ICON_LC_TRIANGLE_ALERT, palette.warning};
                case ToastLevel::Error:
                    return {ICON_LC_CIRCLE_X, palette.error};
            }
            return {ICON_LC_CIRCLE_X, palette.error};
        }

        /// One card between @p min and @p max in the current window, at the opacity of the style
        void DrawCard(const Toast& toast, ImVec2 min, ImVec2 max) {
            const ImGuiPalette& palette = GetImGuiPalette();
            const ImGuiStyle& style = ImGui::GetStyle();
            const LevelLook look = LookOf(toast.level, palette);
            ImDrawList* const drawList = ImGui::GetWindowDrawList();

            drawList->AddRectFilled(min, max, ImGui::GetColorU32(palette.frameBg), style.FrameRounding);
            drawList->AddRect(min, max, ImGui::GetColorU32(look.color), style.FrameRounding);

            const float textTop = min.y + (max.y - min.y - ImGui::GetFontSize()) * 0.5f;
            const ImVec2 iconPosition(min.x + style.WindowPadding.x, textTop);
            drawList->AddText(iconPosition, ImGui::GetColorU32(look.color), look.icon);
            const float textLeft = iconPosition.x + ImGui::CalcTextSize(look.icon).x + style.ItemInnerSpacing.x;

            float textRight = max.x - style.WindowPadding.x;
            if (toast.count > 1) {
                char count[kCountTextSize] = {};
                std::snprintf(count, sizeof(count), "x%u", toast.count);
                textRight -= ImGui::CalcTextSize(count).x;
                drawList->AddText(ImVec2(textRight, textTop), ImGui::GetColorU32(ImGuiCol_TextDisabled), count);
                textRight -= style.ItemInnerSpacing.x;
            }

            // The text may hold what a client wrote: it is drawn as it is, its first line only
            const size_t lineEnd = toast.text.find('\n');
            const char* const text = toast.text.c_str();
            const char* const textEnd = text + (lineEnd == String::npos ? toast.text.size() : lineEnd);
            ImGui::RenderTextEllipsis(drawList, ImVec2(textLeft, textTop), ImVec2(textRight, max.y), textRight, text, textEnd, nullptr);
        }

    }  // namespace

    void ToastOverlay::Draw(float bottomInset) {
        _feed.Update(ImGui::GetIO().DeltaTime);
        const ToastList& list = _feed.GetList();
        if (list.Size() == 0) {
            return;
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImGuiViewport* const viewport = ImGui::GetMainViewport();
        const float margin = ImGui::GetFontSize() * kMarginInEm;
        const float cardHeight = ImGui::GetFrameHeight() + style.FramePadding.y * 2.0f;
        const float gap = style.ItemSpacing.y;
        const float count = static_cast<float>(list.Size());
        const ImVec2 size(eastl::min(ImGui::GetFontSize() * kCardWidthInEm, viewport->WorkSize.x - margin * 2.0f), cardHeight * count + gap * (count - 1.0f));
        const ImVec2 corner(viewport->WorkPos.x + viewport->WorkSize.x - margin, viewport->WorkPos.y + viewport->WorkSize.y - bottomInset - margin);

        ImGui::SetNextWindowPos(corner, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin(kWindowName, nullptr, kWindowFlags);
        ImGui::PopStyleVar(2);
        // A window that appears or takes the focus later goes above this one, so it moves up again every frame
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        ImVec2 min = ImGui::GetWindowPos();
        for (size_t i = 0; i < list.Size(); ++i) {
            const Toast& toast = list.At(i);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * ToastList::GetAlpha(toast));
            DrawCard(toast, min, ImVec2(min.x + size.x, min.y + cardHeight));
            ImGui::PopStyleVar();
            min.y += cardHeight + gap;
        }
        ImGui::End();
    }

}  // namespace BECore
