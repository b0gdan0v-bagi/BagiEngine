#include "ProfilerFramePlot.h"

#include <EASTL/algorithm.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kPlotHeight = 90.0f;
        constexpr float kMaxBarWidth = 12.0f;
        constexpr float kBarGap = 1.0f;
        constexpr float kBudgetComboWidth = 170.0f;
        constexpr float kCustomFieldWidth = 80.0f;
        constexpr float kCustomMinMs = 1.0f;
        constexpr float kCustomMaxMs = 1000.0f;
        constexpr double kScaleInBudgets = 2.5;  // the top of the plot, so the budget line and its double always fit
        constexpr double kScaleHeadroom = 1.1;
        constexpr double kOverBudgetFactor = 2.0;  // a frame over this many budgets is the red one

        constexpr ImU32 kBackgroundColor = IM_COL32(20, 20, 24, 255);
        constexpr ImU32 kWithinColor = IM_COL32(70, 190, 100, 255);
        constexpr ImU32 kOverColor = IM_COL32(230, 180, 60, 255);
        constexpr ImU32 kWayOverColor = IM_COL32(225, 80, 70, 255);
        constexpr ImU32 kBudgetLineColor = IM_COL32(255, 255, 255, 150);
        constexpr ImU32 kDoubleLineColor = IM_COL32(255, 255, 255, 70);
        constexpr ImU32 kSelectedColor = IM_COL32(255, 255, 255, 255);
        constexpr ImU32 kHoverColor = IM_COL32(255, 255, 255, 60);

        const char* const kBudgetLabels = "16.6 ms (60 fps)\0"
                                          "33.3 ms (30 fps)\0"
                                          "Custom\0";

        ImU32 BarColor(double frameMs, double budgetMs) {
            if (frameMs <= budgetMs) {
                return kWithinColor;
            }
            return frameMs <= budgetMs * kOverBudgetFactor ? kOverColor : kWayOverColor;
        }

    }  // namespace

    double ProfilerFramePlot::GetBudgetMs() const {
        switch (_budget) {
            case Budget::Fps60:
                return kBudget60Ms;
            case Budget::Fps30:
                return kBudget30Ms;
            case Budget::Custom:
                break;
        }
        return static_cast<double>(_customBudgetMs);
    }

    std::optional<size_t> ProfilerFramePlot::RingFramePosition(size_t barIndex, size_t barCount, size_t ringFrameCount) {
        if (barIndex >= barCount) {
            return std::nullopt;
        }
        const size_t fromNewest = barCount - 1 - barIndex;
        if (fromNewest >= ringFrameCount) {
            return std::nullopt;
        }
        return ringFrameCount - 1 - fromNewest;
    }

    std::optional<uint64_t> ProfilerFramePlot::Draw(const eastl::vector<FrameStats>& history, const RingSnapshot& snapshot, std::optional<uint64_t> selectedFrame) {
        int budgetChoice = static_cast<int>(_budget);
        ImGui::SetNextItemWidth(kBudgetComboWidth);
        if (ImGui::Combo("Budget", &budgetChoice, kBudgetLabels)) {
            _budget = static_cast<Budget>(budgetChoice);
        }
        if (_budget == Budget::Custom) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(kCustomFieldWidth);
            ImGui::DragFloat("ms##budget", &_customBudgetMs, 0.1f, kCustomMinMs, kCustomMaxMs, "%.1f");
        }

        const double budgetMs = GetBudgetMs();
        const size_t barCount = history.size();
        const ImVec2 size(ImGui::GetContentRegionAvail().x, kPlotHeight);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##frameBars", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        double scaleMs = budgetMs * kScaleInBudgets;
        for (const FrameStats& frame : history) {
            scaleMs = eastl::max(scaleMs, frame.frameMs * kScaleHeadroom);
        }
        const float barWidth = barCount == 0 ? kMaxBarWidth : eastl::min(kMaxBarWidth, size.x / static_cast<float>(barCount));
        const float bottom = origin.y + size.y;
        const auto yOf = [&](double ms) { return bottom - static_cast<float>(eastl::min(ms / scaleMs, 1.0)) * size.y; };

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, bottom), kBackgroundColor);
        const std::optional<size_t> selectedPosition = [&]() -> std::optional<size_t> {
            if (!selectedFrame || snapshot.frames.empty() || *selectedFrame < snapshot.frames.front().index) {
                return std::nullopt;
            }
            return static_cast<size_t>(*selectedFrame - snapshot.frames.front().index);
        }();
        for (size_t bar = 0; bar < barCount; ++bar) {
            const float x = origin.x + static_cast<float>(bar) * barWidth;
            const ImVec2 min(x, yOf(history[bar].frameMs));
            const ImVec2 max(x + eastl::max(barWidth - kBarGap, 1.0f), bottom);
            drawList->AddRectFilled(min, max, BarColor(history[bar].frameMs, budgetMs));
            const std::optional<size_t> position = RingFramePosition(bar, barCount, snapshot.frames.size());
            if (position && selectedPosition && *position == *selectedPosition) {
                drawList->AddRect(ImVec2(min.x - 1.0f, min.y - 1.0f), ImVec2(max.x + 1.0f, max.y), kSelectedColor);
            }
        }
        drawList->AddLine(ImVec2(origin.x, yOf(budgetMs)), ImVec2(origin.x + size.x, yOf(budgetMs)), kBudgetLineColor);
        drawList->AddLine(ImVec2(origin.x, yOf(budgetMs * kOverBudgetFactor)), ImVec2(origin.x + size.x, yOf(budgetMs * kOverBudgetFactor)), kDoubleLineColor);

        std::optional<uint64_t> result;
        if (hovered && barCount > 0 && barWidth > 0.0f) {
            const float mouseX = ImGui::GetMousePos().x - origin.x;
            const size_t bar = eastl::min(static_cast<size_t>(eastl::max(mouseX, 0.0f) / barWidth), barCount - 1);
            const float x = origin.x + static_cast<float>(bar) * barWidth;
            drawList->AddRectFilled(ImVec2(x, origin.y), ImVec2(x + barWidth, bottom), kHoverColor);
            const std::optional<size_t> position = RingFramePosition(bar, barCount, snapshot.frames.size());
            ImGui::SetTooltip("Frame %llu: %.2f ms (cpu %.2f ms)%s", static_cast<unsigned long long>(history[bar].frame), history[bar].frameMs, history[bar].cpuMs,
                              position ? "" : "\nnot in the ring");
            if (clicked && position) {
                result = snapshot.frames[*position].index;
            }
        }
        return result;
    }

}  // namespace BECore
