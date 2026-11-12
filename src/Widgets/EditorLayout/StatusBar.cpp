#include "StatusBar.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Games/GameManager.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr ImGuiWindowFlags kBarWindowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        /// The space between two items of a group, in ems (font sizes)
        constexpr float kItemGapInEm = 1.5f;
        /// The space between the left group and the right one when the window is too narrow for both, in ems
        constexpr float kGroupGapInEm = 2.0f;
        constexpr size_t kMaxRightItems = 4;

        constexpr const char* kFrameTooltip = "Frame rate and frame time (mean of the last frames); the colour is the 60 fps budget";
        constexpr const char* kAllocTooltip = "Heap allocations of the last frame";
        constexpr const char* kControlTooltip = "Control API: the port and the connected clients";
        constexpr const char* kGameTooltip = "Active game";
        constexpr const char* kSceneTooltip = "Active scene";
        constexpr const char* kBranchTooltip = "Git branch of the engine";

        /// One icon and its text; the text is drawn in @p color
        struct BarItem {
            const char* icon = nullptr;
            const char* text = nullptr;
            ImVec4 color;
            bool mono = false;
            const char* tooltip = nullptr;
        };

        ImVec4 LevelColor(FrameLevel level, const ImGuiPalette& palette) {
            switch (level) {
                case FrameLevel::Within:
                    return palette.success;
                case FrameLevel::Over:
                    return palette.warning;
                case FrameLevel::WayOver:
                    return palette.error;
            }
            return palette.error;
        }

        /// The mono font while it lives, for an item that asks for it
        ImFont* MonoFontFor(const BarItem& item) {
            return item.mono ? GetImGuiFonts().mono : nullptr;
        }

        float MeasureItem(const BarItem& item) {
            const float iconWidth = ImGui::CalcTextSize(item.icon).x;
            const ScopedMonoFont mono(MonoFontFor(item));
            return iconWidth + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(item.text).x;
        }

        void DrawItem(const BarItem& item, const ImVec4& iconColor) {
            ImGui::PushStyleColor(ImGuiCol_Text, iconColor);
            ImGui::TextUnformatted(item.icon);
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            {
                const ScopedMonoFont mono(MonoFontFor(item));
                ImGui::PushStyleColor(ImGuiCol_Text, item.color);
                ImGui::TextUnformatted(item.text);
                ImGui::PopStyleColor();
            }
            ItemTooltip(item.tooltip);
        }

        /// Copies @p name into @p target only when it is not the one seen last, so a steady frame does not touch the heap
        void CopyIfChanged(PoolString name, PoolString& seen, eastl::string& target) {
            if (name == seen) {
                return;
            }
            seen = name;
            const eastl::string_view view = name.ToStringView();
            target.assign(view.data(), view.size());
        }

    }  // namespace

    float StatusBar::GetHeight() {
        return ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.y * 2.0f;
    }

    void StatusBar::SetBranch(eastl::string_view branch) {
        _branch.assign(branch.data(), branch.size());
    }

    void StatusBar::Refresh() {
        if (const FrameStats* last = CoreManager::GetFrameStats().GetLast()) {
            _meter.Feed(*last);
            _allocCount = last->allocCount;
        }
        if (_meter.Version() != _shownMeterVersion) {
            _shownMeterVersion = _meter.Version();
            FormatFps(_meter.GetFps(), _fpsText);
            FormatFrameMs(_meter.GetFrameMs(), _frameMsText);
        }
        if constexpr (AllocTracker::kEnabled) {
            if (_shownAllocCount != _allocCount) {
                _shownAllocCount = _allocCount;
                FormatAllocs(_allocCount, _allocText);
            }
        }

        _controlStatus = CoreManager::GetControlService().GetServerStatus();
        if (_shownControlStatus != _controlStatus) {
            _shownControlStatus = _controlStatus;
            FormatControlStatus(_controlStatus.listening, _controlStatus.port, _controlStatus.clients, _controlText);
        }

        const IGame* game = GameManager::GetInstance().GetActive();
        CopyIfChanged(game ? game->GetName() : PoolString(), _seenGame, _game);
        const Scene* scene = CoreManager::GetSceneManager().GetActiveScene();
        CopyIfChanged(scene ? scene->GetName() : PoolString(), _seenScene, _scene);
    }

    void StatusBar::Draw() {
        Refresh();

        const ImGuiPalette& palette = GetImGuiPalette();
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x, style.FramePadding.y));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.panelBg);
        const bool visible = ImGui::BeginChild("##StatusBar", ImVec2(0.0f, GetHeight()), ImGuiChildFlags_AlwaysUseWindowPadding, kBarWindowFlags);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (visible) {
            DrawContent();
        }
        ImGui::EndChild();
    }

    void StatusBar::DrawContent() {
        const ImGuiPalette& palette = GetImGuiPalette();
        const ImVec4 disabled = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        const ImVec4 normal = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        const float itemGap = ImGui::GetFontSize() * kItemGapInEm;
        const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

        ImGui::AlignTextToFramePadding();

        // Left: frame rate and time in the colour of their level, then the allocations of the last frame
        const ImVec4 levelColor = LevelColor(_meter.GetLevel(), palette);
        const BarItem fps{ICON_LC_ACTIVITY, _fpsText, levelColor, true, kFrameTooltip};
        DrawItem(fps, disabled);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
        {
            const ScopedMonoFont mono;
            ImGui::PushStyleColor(ImGuiCol_Text, levelColor);
            ImGui::TextUnformatted(_frameMsText);
            ImGui::PopStyleColor();
            ItemTooltip(kFrameTooltip);
        }
        if constexpr (AllocTracker::kEnabled) {
            ImGui::SameLine(0.0f, itemGap);
            const BarItem allocs{ICON_LC_PACKAGE, _allocText, _allocCount == 0 ? disabled : palette.warning, true, kAllocTooltip};
            DrawItem(allocs, disabled);
        }

        // Right: the control API, the game, the scene and the branch (each only when there is one)
        BarItem right[kMaxRightItems];
        size_t rightCount = 0;
        right[rightCount++] = BarItem{ICON_LC_PLUG, _controlText, _controlStatus.listening ? normal : disabled, true, kControlTooltip};
        if (!_game.empty()) {
            right[rightCount++] = BarItem{ICON_LC_GAMEPAD_2, _game.c_str(), normal, false, kGameTooltip};
        }
        if (!_scene.empty()) {
            right[rightCount++] = BarItem{ICON_LC_CLAPPERBOARD, _scene.c_str(), normal, false, kSceneTooltip};
        }
        if (!_branch.empty()) {
            right[rightCount++] = BarItem{ICON_LC_GIT_BRANCH, _branch.c_str(), normal, false, kBranchTooltip};
        }

        float rightWidth = itemGap * static_cast<float>(rightCount - 1);
        for (size_t i = 0; i < rightCount; ++i) {
            rightWidth += MeasureItem(right[i]);
        }
        ImGui::SameLine();
        const float left = ImGui::GetCursorPosX() + ImGui::GetFontSize() * (kGroupGapInEm - 1.0f);
        ImGui::SetCursorPosX(eastl::max(left, rightEdge - rightWidth));
        for (size_t i = 0; i < rightCount; ++i) {
            if (i > 0) {
                ImGui::SameLine(0.0f, itemGap);
            }
            DrawItem(right[i], disabled);
        }
    }

}  // namespace BECore
