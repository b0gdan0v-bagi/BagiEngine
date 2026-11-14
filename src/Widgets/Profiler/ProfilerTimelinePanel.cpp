#include "ProfilerTimelinePanel.h"

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Profiler/FrameStats/FrameStatsText.h>
#include <EASTL/algorithm.h>
#include <cmath>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kTimelineHeight = 280.0f;
        constexpr float kRowPadding = 2.0f;
        constexpr float kBarGap = 1.0f;
        constexpr float kMinLabelWidth = 36.0f;
        constexpr float kLabelPadding = 3.0f;
        constexpr float kZoomPerNotch = 0.8f;
        constexpr float kRulerLabelSpacing = 110.0f;  // pixels between two labels of the ruler
        constexpr float kRulerTickHeight = 4.0f;
        constexpr double kNiceSteps[] = {1.0, 2.0, 5.0, 10.0};
        constexpr double kMillisecondsPerNanosecond = 1.0e-6;
        constexpr float kHueScale = 360.0f;
        constexpr uint32_t kHueBuckets = 360;
        constexpr float kHashSaturation = 0.5f;
        constexpr float kHashValue = 0.62f;
        constexpr uint32_t kFnvOffset = 2166136261u;
        constexpr uint32_t kFnvPrime = 16777619u;
        constexpr uint32_t kByteMask = 0xFFu;
        constexpr uint32_t kRedShift = 16;
        constexpr uint32_t kGreenShift = 8;

        constexpr ImU32 kBarTextColor = IM_COL32(255, 255, 255, 235);
        constexpr ImU32 kHeaderColor = IM_COL32(200, 200, 210, 255);
        constexpr ImU32 kRulerColor = IM_COL32(150, 150, 160, 255);
        constexpr ImU32 kRulerGridColor = IM_COL32(255, 255, 255, 22);
        constexpr ImU32 kFrameLineColor = IM_COL32(255, 220, 90, 110);
        constexpr ImU32 kMarkedColor = IM_COL32(255, 255, 255, 255);
        constexpr float kMarkedThickness = 2.0f;

        ImU32 ZoneColor(const ProfileSite& site) {
            if (site.color != 0) {
                return IM_COL32((site.color >> kRedShift) & kByteMask, (site.color >> kGreenShift) & kByteMask, site.color & kByteMask, 255);
            }
            uint32_t hash = kFnvOffset;
            for (const char* c = site.name; *c != '\0'; ++c) {
                hash = (hash ^ static_cast<uint8_t>(*c)) * kFnvPrime;
            }
            return ImColor::HSV(static_cast<float>(hash % kHueBuckets) / kHueScale, kHashSaturation, kHashValue);
        }

        /// The step of a ruler of 1, 2, 5, 10, ... times a power of ten that is at least @p minimumMs
        double NiceStepMs(double minimumMs) {
            const double decade = std::pow(10.0, std::floor(std::log10(minimumMs)));
            for (const double step : kNiceSteps) {
                if (step * decade >= minimumMs) {
                    return step * decade;
                }
            }
            return decade * kNiceSteps[std::size(kNiceSteps) - 1];
        }

    }  // namespace

    void ProfilerTimelinePanel::Reset() {
        _follow = true;
        _fitRequested = false;
        _shownSelection.reset();
        _markedSite = nullptr;
        _view = TimelineView();
    }

    void ProfilerTimelinePanel::ShowZone(const ZoneRecord& zone) {
        _markedSite = zone.site;
        _markedBeginTicks = zone.beginTicks;
        _follow = false;
        _fitRequested = true;
    }

    void ProfilerTimelinePanel::FitFrame(const RingFrame& frame) {
        _view.FitTo(frame.beginTicks, frame.endTicks);
    }

    void ProfilerTimelinePanel::Draw(const RingSnapshot& snapshot, Optional<uint64_t> selectedFrame) {
        if (snapshot.frames.empty()) {
            ImGui::TextUnformatted("No frame in the ring yet");
            return;
        }
        const TickRange extent = SnapshotExtent(snapshot);
        _view.SetExtent(extent.begin, extent.end, snapshot.nanosecondsPerTick);

        const Optional<size_t> selectedPosition = FramePosition(snapshot, selectedFrame);
        const RingFrame* selected = selectedPosition ? &snapshot.frames[*selectedPosition] : nullptr;
        const RingFrame& frame = selected != nullptr ? *selected : snapshot.frames.back();
        if (selectedFrame != _shownSelection) {
            // A click on another bar opens that frame; no selection goes back to the newest one
            _shownSelection = selectedFrame;
            _follow = selected == nullptr;
            _fitRequested = true;
        }
        if (_fitRequested || (_follow && selected == nullptr)) {
            FitFrame(frame);
            _fitRequested = false;
        }
        DrawToolbar(snapshot, frame, selected != nullptr);
        DrawCanvas(snapshot, frame);
    }

    void ProfilerTimelinePanel::DrawToolbar(const RingSnapshot& snapshot, const RingFrame& frame, bool hasSelection) {
        if (ImGui::Button("Fit")) {
            FitFrame(frame);
            _follow = !hasSelection;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(hasSelection);
        if (ImGui::Checkbox("Follow newest", &_follow) && _follow) {
            FitFrame(frame);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const double viewMs = snapshot.TicksToMs(static_cast<uint64_t>(_view.SpanTicks()));
        ImGui::Text("Frame %llu, %.2f ms; view %.3f ms", static_cast<unsigned long long>(frame.index), snapshot.TicksToMs(frame.endTicks - frame.beginTicks), viewMs);
    }

    void ProfilerTimelinePanel::DrawRuler(const RingSnapshot& snapshot, const RingFrame& frame, const ImVec2& origin, float width, float gridHeight) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const double ticksPerMs = 1.0 / (snapshot.nanosecondsPerTick * kMillisecondsPerNanosecond);
        const double viewMs = _view.SpanTicks() / ticksPerMs;
        const double stepMs = NiceStepMs(viewMs / static_cast<double>(width) * static_cast<double>(kRulerLabelSpacing));
        // Times are counted from the beginning of the frame the panel is on
        const double firstMs = (_view.BeginTicks() - static_cast<double>(frame.beginTicks)) / ticksPerMs;
        const double lastMs = (_view.EndTicks() - static_cast<double>(frame.beginTicks)) / ticksPerMs;
        const float rulerHeight = ImGui::GetTextLineHeight();
        for (double ms = std::ceil(firstMs / stepMs) * stepMs; ms <= lastMs; ms += stepMs) {
            const double tick = static_cast<double>(frame.beginTicks) + ms * ticksPerMs;
            const float x = origin.x + _view.TickToX(tick, width);
            drawList->AddLine(ImVec2(x, origin.y + rulerHeight - kRulerTickHeight), ImVec2(x, origin.y + rulerHeight), kRulerColor);
            drawList->AddLine(ImVec2(x, origin.y + rulerHeight), ImVec2(x, origin.y + rulerHeight + gridHeight), kRulerGridColor);
            char label[32];
            snprintf(label, sizeof(label), "%g ms", ms);
            drawList->AddText(ImVec2(x + kLabelPadding, origin.y), kRulerColor, label);
        }
    }

    void ProfilerTimelinePanel::DrawCanvas(const RingSnapshot& snapshot, const RingFrame& frame) {
        ImGui::BeginChild("##timeline", ImVec2(0.0f, kTimelineHeight), ImGuiChildFlags_Borders);
        const float width = eastl::max(ImGui::GetContentRegionAvail().x, 1.0f);
        const float rowHeight = ImGui::GetTextLineHeight() + kRowPadding;
        const float headerHeight = ImGui::GetTextLineHeight();  // the lane's name, above its rows: the gap of LaneTop
        const float rulerHeight = ImGui::GetTextLineHeight();
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        _lanes = LayoutLanes(snapshot, _view.BeginTicks(), _view.EndTicks());
        const float lanesHeight = LaneTop(_lanes, _lanes.size(), rowHeight, headerHeight);
        ImGui::InvisibleButton("##canvas", ImVec2(width, rulerHeight + eastl::max(lanesHeight, 1.0f)), ImGuiButtonFlags_MouseButtonLeft);
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        const bool hovered = ImGui::IsItemHovered();
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 mouse(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
        const double pixelsPerTick = _view.PixelsPerTick(width);

        if (hovered && io.MouseWheel != 0.0f) {
            _view.ZoomAt(_view.XToTick(mouse.x, width), std::pow(kZoomPerNotch, io.MouseWheel));
            _follow = false;
        }
        if (ImGui::IsItemActive() && io.MouseDelta.x != 0.0f && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            _view.PanByTicks(-static_cast<double>(io.MouseDelta.x) / pixelsPerTick);
            _follow = false;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        DrawRuler(snapshot, frame, origin, width, lanesHeight);
        const float lanesTop = origin.y + rulerHeight;
        // The marks that cut the ring into frames
        for (const RingFrame& mark : snapshot.frames) {
            const double begin = static_cast<double>(mark.beginTicks);
            if (begin >= _view.BeginTicks() && begin <= _view.EndTicks()) {
                const float x = origin.x + _view.TickToX(begin, width);
                drawList->AddLine(ImVec2(x, lanesTop), ImVec2(x, lanesTop + lanesHeight), kFrameLineColor);
            }
        }

        for (size_t laneIndex = 0; laneIndex < _lanes.size(); ++laneIndex) {
            const RingThread& thread = snapshot.threads[_lanes[laneIndex].threadIndex];
            const float headerTop = lanesTop + LaneTop(_lanes, laneIndex, rowHeight, headerHeight);
            const float laneTop = headerTop + headerHeight;
            const String title = thread.name.empty() ? Format("Thread {}", thread.threadId) : thread.name;
            drawList->AddText(ImVec2(origin.x + kLabelPadding, headerTop), kHeaderColor, title.c_str());
            for (const ZoneRecord& zone : thread.zones) {
                if (static_cast<double>(zone.endTicks) < _view.BeginTicks() || static_cast<double>(zone.beginTicks) > _view.EndTicks() || !IsVisible(zone, pixelsPerTick)) {
                    continue;
                }
                const float x0 = origin.x + _view.TickToX(static_cast<double>(zone.beginTicks), width);
                const float x1 = origin.x + _view.TickToX(static_cast<double>(zone.endTicks), width);
                const float y0 = laneTop + static_cast<float>(zone.depth) * rowHeight;
                const ImVec2 min(eastl::max(x0, origin.x), y0);
                const ImVec2 max(eastl::min(eastl::max(x1 - kBarGap, x0 + 1.0f), origin.x + width), y0 + rowHeight - 1.0f);
                if (max.x <= min.x) {
                    continue;
                }
                drawList->AddRectFilled(min, max, ZoneColor(*zone.site));
                if (zone.site == _markedSite && zone.beginTicks == _markedBeginTicks) {
                    drawList->AddRect(min, max, kMarkedColor, 0.0f, ImDrawFlags_None, kMarkedThickness);
                }
                if (max.x - min.x >= kMinLabelWidth) {
                    drawList->PushClipRect(min, max, true);
                    drawList->AddText(ImVec2(min.x + kLabelPadding, y0), kBarTextColor, zone.site->name);
                    drawList->PopClipRect();
                }
            }
        }

        if (hovered) {
            // The hit test counts y from the top of the first lane's rows; each lane has its name above it, which is the gap
            const auto hit = HitTest(_lanes, snapshot, _view, mouse.x, mouse.y - rulerHeight - headerHeight, rowHeight, width, headerHeight);
            if (hit) {
                const ZoneRecord& zone = snapshot.threads[hit->threadIndex].zones[hit->zoneIndex];
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(zone.site->name);
                ImGui::Text("%s:%u", zone.site->file, zone.site->line);
                ImGui::Text("%.3f ms", snapshot.DurationMs(zone));
                if (AllocTracker::kEnabled) {
                    ImGui::Text("%llu allocations, %s", static_cast<unsigned long long>(zone.allocCount), FormatBytes(zone.allocBytes).c_str());
                }
                ImGui::EndTooltip();
            }
        }
        ImGui::EndChild();
    }

}  // namespace BECore
