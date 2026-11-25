#include "ProfilerFramePlot.h"

#include <BECore/Profiler/Ring/TimelineView.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/Profiler/ProfilerPanelModel.h>
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

        constexpr float kBarModeComboWidth = 140.0f;
        constexpr float kCounterGraphHeight = 40.0f;
        constexpr float kStageSwatchSize = 10.0f;
        constexpr float kStageSwatchOffsetY = 2.0f;  // centres the swatch on the text line
        constexpr ImU32 kMarkerColor = IM_COL32(255, 255, 255, 200);

        // One fixed color per segment of ExclusiveStage
        constexpr Array<ImU32, kExclusiveStageCount> kStageColors{IM_COL32(90, 140, 220, 255),  IM_COL32(60, 200, 210, 255),  IM_COL32(220, 130, 60, 255), IM_COL32(230, 200, 70, 255),
                                                                  IM_COL32(170, 100, 210, 255), IM_COL32(130, 130, 130, 255), IM_COL32(190, 205, 225, 255)};
        constexpr Array<const char*, kExclusiveStageCount> kStageNames{"Update", "Scene update", "Render", "Scene draw", "Present", "Other", "Wait"};

        const char* const kBarModeLabels = "Frame time\0"
                                           "Stage split\0";

        const char* const kBudgetLabels = "16.6 ms (60 fps)\0"
                                          "33.3 ms (30 fps)\0"
                                          "Custom\0";

        /// A held frame (the game stood still) is drawn in the theme's muted fill, so it is not read as a cheap game frame
        ImU32 PausedBarColor() {
            return ImGui::ColorConvertFloat4ToU32(GetImGuiPalette().accentMuted);
        }

        ImU32 BarColor(double frameMs, double budgetMs) {
            if (frameMs <= budgetMs) {
                return kWithinColor;
            }
            return frameMs <= budgetMs * kOverBudgetFactor ? kOverColor : kWayOverColor;
        }

        double BudgetOf(ProfilerBudget budget, float customMs) {
            switch (budget) {
                case ProfilerBudget::Fps60:
                    return ProfilerFramePlot::kBudget60Ms;
                case ProfilerBudget::Fps30:
                    return ProfilerFramePlot::kBudget30Ms;
                case ProfilerBudget::Custom:
                    break;
            }
            return static_cast<double>(customMs);
        }

    }  // namespace

    double ProfilerFramePlot::GetBudgetMs() const {
        return BudgetOf(_budget, _customBudgetMs);
    }

    double BudgetMs(const ProfilerPanelView& view) {
        return BudgetOf(static_cast<ProfilerBudget>(view.budget), view.customBudgetMs);
    }

    ProfilerFramePlot::Settings PlotSettingsOf(const ProfilerPanelView& view) {
        ProfilerFramePlot::Settings settings;
        settings.budgetMs = BudgetMs(view);
        settings.barMode = static_cast<ProfilerBarMode>(view.barMode);
        settings.workOnly = view.workOnly;
        return settings;
    }

    Optional<size_t> ProfilerFramePlot::RingFramePosition(size_t barIndex, size_t barCount, size_t ringFrameCount) {
        if (barIndex >= barCount) {
            return std::nullopt;
        }
        const size_t fromNewest = barCount - 1 - barIndex;
        if (fromNewest >= ringFrameCount) {
            return std::nullopt;
        }
        return ringFrameCount - 1 - fromNewest;
    }

    Optional<uint64_t> ProfilerFramePlot::Draw(const Vector<FrameStats>& history, const RingSnapshot& snapshot, Optional<uint64_t> selectedFrame) {
        int budgetChoice = static_cast<int>(+_budget);
        ImGui::SetNextItemWidth(kBudgetComboWidth);
        if (ImGui::Combo("Budget", &budgetChoice, kBudgetLabels)) {
            _budget = static_cast<ProfilerBudget>(budgetChoice);
        }
        if (_budget == ProfilerBudget::Custom) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(kCustomFieldWidth);
            ImGui::DragFloat("ms##budget", &_customBudgetMs, 0.1f, kCustomMinMs, kCustomMaxMs, "%.1f");
        }

        ImGui::SameLine();
        int modeChoice = static_cast<int>(+_barMode);
        ImGui::SetNextItemWidth(kBarModeComboWidth);
        if (ImGui::Combo("Bars", &modeChoice, kBarModeLabels)) {
            _barMode = static_cast<ProfilerBarMode>(modeChoice);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Work only", &_workOnly);
        ImGui::SetItemTooltip("Leave out the time the backend waited for the display (the Wait stage), so a bar is what the CPU did");

        return DrawBars(history, snapshot, selectedFrame, Settings{GetBudgetMs(), _barMode, _workOnly});
    }

    Optional<uint64_t> ProfilerFramePlot::DrawBars(const Vector<FrameStats>& history, const RingSnapshot& snapshot, Optional<uint64_t> selectedFrame, const Settings& settings) {
        // The bar height of a frame: its time, or without the wait
        const auto shownMs = [&settings](const FrameStats& frame) { return settings.workOnly ? FrameWorkMs(frame) : frame.frameMs; };

        const double budgetMs = settings.budgetMs;
        const size_t barCount = history.size();
        const ImVec2 size(ImGui::GetContentRegionAvail().x, kPlotHeight);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##frameBars", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        double scaleMs = budgetMs * kScaleInBudgets;
        for (const FrameStats& frame : history) {
            scaleMs = eastl::max(scaleMs, shownMs(frame) * kScaleHeadroom);
        }
        const float barWidth = barCount == 0 ? kMaxBarWidth : eastl::min(kMaxBarWidth, size.x / static_cast<float>(barCount));
        const float bottom = origin.y + size.y;
        const auto yOf = [&](double ms) { return bottom - static_cast<float>(eastl::min(ms / scaleMs, 1.0)) * size.y; };

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, bottom), kBackgroundColor);
        const Optional<size_t> selectedPosition = FramePosition(snapshot, selectedFrame);
        for (size_t bar = 0; bar < barCount; ++bar) {
            const float x = origin.x + static_cast<float>(bar) * barWidth;
            Array<double, kExclusiveStageCount> stages{};
            if (settings.barMode == ProfilerBarMode::StageSplit) {
                stages = ExclusiveStageMs(history[bar]);
            }
            ImVec2 min(x, yOf(shownMs(history[bar])));
            const ImVec2 max(x + eastl::max(barWidth - kBarGap, 1.0f), bottom);
            if (history[bar].paused) {
                drawList->AddRectFilled(min, max, PausedBarColor());
            } else if (settings.barMode == ProfilerBarMode::StageSplit) {
                // The stack is as high as the frame's cpu time, the segments from the bottom up
                float segmentBottom = bottom;
                // Wait is the last segment, so work only is every segment but the last
                const size_t segmentCount = settings.workOnly ? kExclusiveStageCount - 1 : kExclusiveStageCount;
                for (size_t segment = 0; segment < segmentCount; ++segment) {
                    const float top = eastl::max(segmentBottom - static_cast<float>(eastl::min(stages[segment] / scaleMs, 1.0)) * size.y, origin.y);
                    drawList->AddRectFilled(ImVec2(min.x, top), ImVec2(max.x, segmentBottom), kStageColors[segment]);
                    segmentBottom = top;
                }
                min.y = segmentBottom;
            } else {
                drawList->AddRectFilled(min, max, BarColor(shownMs(history[bar]), budgetMs));
            }
            const Optional<size_t> position = RingFramePosition(bar, barCount, snapshot.frames.size());
            if (position && selectedPosition && *position == *selectedPosition) {
                drawList->AddRect(ImVec2(min.x - 1.0f, min.y - 1.0f), ImVec2(max.x + 1.0f, max.y), kSelectedColor);
            }
        }
        drawList->AddLine(ImVec2(origin.x, yOf(budgetMs)), ImVec2(origin.x + size.x, yOf(budgetMs)), kBudgetLineColor);
        drawList->AddLine(ImVec2(origin.x, yOf(budgetMs * kOverBudgetFactor)), ImVec2(origin.x + size.x, yOf(budgetMs * kOverBudgetFactor)), kDoubleLineColor);

        Optional<uint64_t> result;
        if (hovered && barCount > 0 && barWidth > 0.0f) {
            const float mouseX = ImGui::GetMousePos().x - origin.x;
            const size_t bar = eastl::min(static_cast<size_t>(eastl::max(mouseX, 0.0f) / barWidth), barCount - 1);
            const float x = origin.x + static_cast<float>(bar) * barWidth;
            drawList->AddRectFilled(ImVec2(x, origin.y), ImVec2(x + barWidth, bottom), kHoverColor);
            const Optional<size_t> position = RingFramePosition(bar, barCount, snapshot.frames.size());
            ImGui::SetTooltip("Frame %llu: %.2f ms (cpu %.2f ms, wait %.2f ms)%s%s", static_cast<unsigned long long>(history[bar].frame), history[bar].frameMs, history[bar].cpuMs,
                              history[bar].stageMs[+FrameStage::Wait], history[bar].paused ? "\npaused: the game stood still" : "", position ? "" : "\nnot in the ring");
            if (clicked && position) {
                result = snapshot.frames[*position].index;
            }
        }
        if (settings.barMode == ProfilerBarMode::StageSplit) {
            DrawStageLegend(settings.workOnly);
        }
        DrawWorkWaitSummary(history);
        DrawCounters(history, snapshot, selectedFrame);
        return result;
    }

    void ProfilerFramePlot::DrawWorkWaitSummary(const Vector<FrameStats>& history) {
        const uint64_t newestFrame = history.empty() ? 0 : history.back().frame;
        if (!_summaryBuilt || newestFrame != _summaryFrame || history.size() != _summaryFrameCount) {
            _summaryBuilt = true;
            _summaryFrame = newestFrame;
            _summaryFrameCount = history.size();
            const WorkWaitMean mean = SummarizeWorkWait(history);
            std::snprintf(_summaryText.data(), _summaryText.size(), "Work mean %.2f ms, Wait mean %.2f ms", mean.workMs, mean.waitMs);
        }
        ImGui::TextUnformatted(_summaryText.data());
    }

    void ProfilerFramePlot::DrawStageLegend(bool workOnly) const {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const size_t segmentCount = workOnly ? kExclusiveStageCount - 1 : kExclusiveStageCount;
        for (size_t segment = 0; segment < segmentCount; ++segment) {
            if (segment > 0) {
                ImGui::SameLine();
            }
            const ImVec2 at = ImGui::GetCursorScreenPos();
            drawList->AddRectFilled(ImVec2(at.x, at.y + kStageSwatchOffsetY), ImVec2(at.x + kStageSwatchSize, at.y + kStageSwatchOffsetY + kStageSwatchSize), kStageColors[segment]);
            ImGui::Dummy(ImVec2(kStageSwatchSize, kStageSwatchSize));
            ImGui::SameLine();
            ImGui::TextUnformatted(kStageNames[segment]);
        }
    }

    void ProfilerFramePlot::DrawCounters(const Vector<FrameStats>& history, const RingSnapshot& snapshot, Optional<uint64_t> selectedFrame) {
        if (!ImGui::CollapsingHeader("Counters")) {
            return;
        }
        const uint64_t newestFrame = history.empty() ? 0 : history.back().frame;
        if (newestFrame != _counterNamesFrame) {
            _counterNamesFrame = newestFrame;
            _counterNames.clear();
            for (const FrameStats& frame : history) {
                for (const FrameCounter& counter : frame.counters) {
                    if (eastl::find(_counterNames.begin(), _counterNames.end(), counter.name) == _counterNames.end()) {
                        _counterNames.push_back(counter.name);
                    }
                }
            }
            eastl::sort(_counterNames.begin(), _counterNames.end(), [](const PoolString& a, const PoolString& b) { return a.ToStringView() < b.ToStringView(); });
        }
        if (_counterNames.empty()) {
            ImGui::TextDisabled("No counters in these frames");
            return;
        }
        for (const PoolString& name : _counterNames) {
            const auto picked = eastl::find(_pickedCounters.begin(), _pickedCounters.end(), name);
            bool isPicked = picked != _pickedCounters.end();
            if (ImGui::Selectable(name.CStr(), isPicked)) {
                if (isPicked) {
                    _pickedCounters.erase(picked);
                } else {
                    _pickedCounters.push_back(name);
                }
            }
        }
        const Optional<size_t> selectedPosition = FramePosition(snapshot, selectedFrame);
        for (const PoolString& name : _pickedCounters) {
            _counterValues.clear();
            for (const FrameStats& frame : history) {
                _counterValues.push_back(static_cast<float>(frame.FindCounter(name).value_or(0.0)));
            }
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            // The same label as the Selectable above: its own ID scope keeps the two apart
            ImGui::PushID(name.CStr());
            ImGui::PlotLines(name.CStr(), _counterValues.data(), static_cast<int>(_counterValues.size()), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(width, kCounterGraphHeight));
            ImGui::PopID();
            // The marker of the selected frame: the plot spreads its points over the width
            if (selectedPosition && _counterValues.size() > 1) {
                for (size_t bar = 0; bar < _counterValues.size(); ++bar) {
                    const Optional<size_t> position = RingFramePosition(bar, _counterValues.size(), snapshot.frames.size());
                    if (position && *position == *selectedPosition) {
                        const float x = origin.x + width * static_cast<float>(bar) / static_cast<float>(_counterValues.size() - 1);
                        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + kCounterGraphHeight), kMarkerColor);
                        break;
                    }
                }
            }
        }
    }

}  // namespace BECore
