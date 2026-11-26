#include "ViewportUIDebug.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UILayoutValidator.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>

namespace BECore {

    namespace {

        constexpr ImU32 WithAlpha(ImU32 color, uint32_t alpha) {
            return (color & ~static_cast<ImU32>(IM_COL32_A_MASK)) | (alpha << IM_COL32_A_SHIFT);
        }

        // The tint of an element rect by its depth in the tree: a child differs from its parent
        constexpr ImU32 kDepthColors[] = {IM_COL32(255, 210, 60, 255), IM_COL32(90, 220, 255, 255), IM_COL32(255, 120, 200, 255), IM_COL32(150, 255, 120, 255)};
        constexpr uint32_t kElementRectAlpha = 150;
        constexpr uint32_t kHoveredFillAlpha = 40;
        constexpr float kHoveredThickness = 2.5f;

        constexpr ImU32 kAnchorColor = IM_COL32(255, 255, 255, 255);
        constexpr uint32_t kFaintAlpha = 70;
        constexpr uint32_t kStrongAlpha = 255;
        constexpr float kFaintCorner = 2.0f;  // half side of an anchor corner marker, in screen units
        constexpr float kStrongCorner = 4.0f;
        constexpr float kFaintPivot = 3.0f;  // arm of the pivot cross
        constexpr float kStrongPivot = 7.0f;
        constexpr float kPivotRing = 4.0f;
        constexpr uint32_t kAnchorFillAlpha = 35;

        constexpr ImU32 kDesignColor = IM_COL32(255, 160, 0, 230);
        constexpr float kDashLength = 8.0f;
        constexpr float kDashGap = 5.0f;
        constexpr float kDesignLabelInset = 4.0f;
        constexpr const char* kDesignLabel = "Design";

        // One colour per UILayoutIssueKind, in the order of the enum
        constexpr ImU32 kFindingColors[] = {
            IM_COL32(255, 70, 70, 255),    // OutsideCanvas
            IM_COL32(255, 150, 40, 255),   // OutsideSafeArea
            IM_COL32(255, 90, 210, 255),   // SiblingsOverlap
            IM_COL32(255, 230, 40, 255),   // TouchTargetTooSmall
            IM_COL32(60, 200, 255, 255),   // TextDoesNotFit
            IM_COL32(170, 110, 255, 255),  // OverlapsSceneView
            IM_COL32(120, 255, 120, 255),  // MissingRegion
            IM_COL32(40, 220, 180, 255),   // OutsideRegion
            IM_COL32(200, 200, 200, 255),  // RegionIgnored
        };
        static_assert(std::size(kFindingColors) == EnumUtils<UILayoutIssueKind>::Count());
        constexpr uint32_t kFindingFillAlpha = 55;
        constexpr uint32_t kFindingLineAlpha = 240;
        constexpr float kFindingThickness = 1.5f;
        constexpr float kHoveredFindingThickness = 3.0f;
        constexpr float kFindingHitSlop = 3.0f;  // screen units: a finding with no area is still pointed at

        constexpr const char* kUnnamed = "(unnamed)";
        constexpr const char* kFindingsWindowTitle = "UI Findings";
        constexpr ImVec2 kFindingsWindowSize{420.0f, 300.0f};

        bool Contains(const Rect& rect, const Vec2& point, float slop) {
            return point.x >= rect.x - slop && point.x < rect.x + rect.w + slop && point.y >= rect.y - slop && point.y < rect.y + rect.h + slop;
        }

        // Output pixels to the two screen corners of a rect
        Pair<ImVec2, ImVec2> Corners(const ViewportFrame& frame, const Rect& rect) {
            return {frame.ToScreen({rect.x, rect.y}), frame.ToScreen({rect.x + rect.w, rect.y + rect.h})};
        }

        void AddCornerMarker(ImDrawList& drawList, const ImVec2& at, float half, ImU32 color) {
            drawList.AddRectFilled({at.x - half, at.y - half}, {at.x + half, at.y + half}, color);
        }

        void AddPivotCross(ImDrawList& drawList, const ImVec2& at, float arm, ImU32 color) {
            drawList.AddLine({at.x - arm, at.y}, {at.x + arm, at.y}, color);
            drawList.AddLine({at.x, at.y - arm}, {at.x, at.y + arm}, color);
        }

        // The anchor rect, its two corners and the pivot of one element; @p strong is for the element under the pointer
        void DrawAnchorMark(ImDrawList& drawList, const ViewportFrame& frame, const AnchorMark& mark, bool strong) {
            const uint32_t alpha = strong ? kStrongAlpha : kFaintAlpha;
            const ImU32 color = WithAlpha(kAnchorColor, alpha);
            const float corner = strong ? kStrongCorner : kFaintCorner;
            const float arm = strong ? kStrongPivot : kFaintPivot;
            if (strong) {
                const auto [boxMin, boxMax] = Corners(frame, mark.box);
                AddDashedRect(drawList, boxMin, boxMax, color);
            }
            const auto [min, max] = Corners(frame, mark.anchors);
            if (max.x > min.x && max.y > min.y) {
                if (strong) {
                    drawList.AddRectFilled(min, max, WithAlpha(kAnchorColor, kAnchorFillAlpha));
                }
                drawList.AddRect(min, max, color);
            }
            AddCornerMarker(drawList, min, corner, color);
            AddCornerMarker(drawList, max, corner, color);
            const ImVec2 pivot = frame.ToScreen(mark.pivot);
            AddPivotCross(drawList, pivot, arm, color);
            if (strong) {
                drawList.AddCircle(pivot, kPivotRing, color);
            }
        }

    }  // namespace

    static void AddDashedLine(ImDrawList& drawList, const ImVec2& from, const ImVec2& to, ImU32 color) {
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0f) {
            return;
        }
        const float stepX = dx / length;
        const float stepY = dy / length;
        for (float at = 0.0f; at < length; at += kDashLength + kDashGap) {
            const float end = std::min(at + kDashLength, length);
            drawList.AddLine({from.x + stepX * at, from.y + stepY * at}, {from.x + stepX * end, from.y + stepY * end}, color);
        }
    }

    void AddDashedRect(ImDrawList& drawList, const ImVec2& min, const ImVec2& max, ImU32 color) {
        AddDashedLine(drawList, min, {max.x, min.y}, color);
        AddDashedLine(drawList, {max.x, min.y}, max, color);
        AddDashedLine(drawList, max, {min.x, max.y}, color);
        AddDashedLine(drawList, {min.x, max.y}, min, color);
    }

    void ViewportUIDebug::CollectCanvases(const SceneNode& node, Vector<IntrusivePtr<UICanvasComponent>>& out) {
        if (node.HasEditorFlag(EditorFlags::Hidden)) {
            return;
        }
        if (auto canvas = node.GetComponent<UICanvasComponent>()) {
            out.push_back(std::move(canvas));
        }
        for (const auto& child : node.GetChildren()) {
            CollectCanvases(*child, out);
        }
    }

    bool ViewportUIDebug::IsShown(const CanvasState& state) {
        return state.enabled && state.canvas->_visible && state.canvas->GetLayout().HasLayout();
    }

    bool ViewportUIDebug::AnyOn(const PreviewOverlays& overlays) {
        return overlays.elementRects || overlays.anchors || overlays.designRect || overlays.findings;
    }

    void ViewportUIDebug::Collect(Scene* scene, FunctionRef<bool(const SceneNode*)> isEnabled) {
        if (!scene) {
            Release();
            return;
        }
        _scene = scene;
        _collected.clear();
        _canvasRows.clear();
        CollectCanvases(scene->GetRootNode(), _collected);

        // The states follow the order of the scene; a canvas that is still there keeps its marks
        size_t place = 0;
        for (const IntrusivePtr<UICanvasComponent>& canvas : _collected) {
            const auto found = std::find_if(_states.begin() + static_cast<ptrdiff_t>(place), _states.end(), [&](const CanvasState& state) { return state.canvas.Get() == canvas.Get(); });
            if (found == _states.end()) {
                _states.insert(_states.begin() + static_cast<ptrdiff_t>(place), CanvasState{});
                _states[place].canvas = canvas;
            } else if (found != _states.begin() + static_cast<ptrdiff_t>(place)) {
                std::swap(*found, _states[place]);
            }
            CanvasState& state = _states[place];
            state.node = &canvas->GetNode();
            state.name = state.node->GetName();
            state.enabled = isEnabled(state.node);
            _canvasRows.push_back({state.node, state.name});
            ++place;
        }
        _states.erase(_states.begin() + static_cast<ptrdiff_t>(place), _states.end());
        _collected.clear();
    }

    void ViewportUIDebug::Release() {
        _states.clear();
        _collected.clear();
        _canvasRows.clear();
        _hoverElement.reset();
        _hoverFinding.reset();
    }

    void ViewportUIDebug::RefreshElements(CanvasState& state) const {
        const UICanvasLayout& layout = state.canvas->GetLayout();
        if (state.elementsValid && state.elementRuns == layout.GetLayoutRuns()) {
            return;
        }
        UIPreviewOverlay::ElementMarks(layout, state.elements);
        state.anchors.clear();
        for (const ElementMark& mark : state.elements) {
            state.anchors.push_back(UIPreviewOverlay::AnchorsOf(layout, *mark.element));
        }
        state.elementRuns = layout.GetLayoutRuns();
        state.elementsValid = true;
    }

    void ViewportUIDebug::RefreshFindings(CanvasState& state, float minTouchSize) const {
        const UICanvasLayout& layout = state.canvas->GetLayout();
        const Vector<UILayoutIssue>& issues = state.findings.Get(layout, minTouchSize);
        if (state.findingsValid && state.findingRuns == state.findings.GetValidationRuns()) {
            return;
        }
        UIPreviewOverlay::FindingMarks(layout, {issues.data(), issues.size()}, state.findingMarks);
        state.findingRuns = state.findings.GetValidationRuns();
        state.findingsValid = true;
    }

    void ViewportUIDebug::FindHoverElement(const ViewportFrame& frame, const ImVec2& mouse) {
        const Vec2 target = frame.ToTarget(mouse);
        // The last canvas of the scene is drawn on top
        for (size_t index = _states.size(); index-- > 0;) {
            const CanvasState& state = _states[index];
            if (!IsShown(state)) {
                continue;
            }
            const UICanvasLayout& layout = state.canvas->GetLayout();
            const auto [canvasX, canvasY] = layout.OutputToCanvas(target.x, target.y);
            if (const UIElement* element = layout.PickElement(canvasX, canvasY)) {
                _hoverElement = HoverElement{index, element};
                return;
            }
        }
    }

    void ViewportUIDebug::Draw(ImDrawList& drawList, const ViewportFrame& frame, const PreviewOverlays& overlays, bool hovered, const ImVec2& mouse) {
        _hoverElement.reset();
        _hoverFinding.reset();
        const Optional<HoverFinding> listHover = _listHover;
        _listHover.reset();
        _minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const bool elements = overlays.elementRects || overlays.anchors;

        for (CanvasState& state : _states) {
            if (!IsShown(state)) {
                continue;
            }
            if (elements) {
                RefreshElements(state);
            }
            if (overlays.findings) {
                RefreshFindings(state, _minTouchSize);
            }
        }

        if (hovered && elements) {
            FindHoverElement(frame, mouse);
        }
        if (hovered && overlays.findings) {
            // The smallest finding under the pointer, so one inside another stays reachable
            const Vec2 target = frame.ToTarget(mouse);
            const float slop = kFindingHitSlop / frame.scale;
            float smallest = 0.0f;
            for (size_t canvas = _states.size(); canvas-- > 0;) {
                const CanvasState& state = _states[canvas];
                if (!IsShown(state) || !state.findingsValid) {
                    continue;
                }
                for (size_t index = 0; index < state.findingMarks.size(); ++index) {
                    const Rect& rect = state.findingMarks[index].rect;
                    const float area = rect.w * rect.h;
                    if (Contains(rect, target, slop) && (!_hoverFinding || area < smallest)) {
                        _hoverFinding = HoverFinding{canvas, index};
                        smallest = area;
                    }
                }
            }
        }

        for (const CanvasState& state : _states) {
            if (!IsShown(state)) {
                continue;
            }
            if (elements) {
                DrawElements(drawList, frame, state, overlays.elementRects, overlays.anchors);
            }
            if (overlays.designRect) {
                DrawDesignRect(drawList, frame, state);
            }
        }
        if (_hoverElement) {
            DrawHoveredElement(drawList, frame, overlays.anchors);
        }
        if (overlays.findings) {
            for (size_t index = 0; index < _states.size(); ++index) {
                const CanvasState& state = _states[index];
                if (IsShown(state) && state.findingsValid) {
                    const Optional<HoverFinding>& highlighted = _hoverFinding ? _hoverFinding : listHover;
                    DrawFindings(drawList, frame, state, highlighted && highlighted->canvas == index ? static_cast<int64_t>(highlighted->index) : -1);
                }
            }
        }
    }

    void ViewportUIDebug::DrawElements(ImDrawList& drawList, const ViewportFrame& frame, const CanvasState& state, bool rects, bool anchors) const {
        for (size_t index = 0; index < state.elements.size(); ++index) {
            const ElementMark& mark = state.elements[index];
            if (rects) {
                const auto [min, max] = Corners(frame, mark.rect);
                drawList.AddRect(min, max, WithAlpha(kDepthColors[mark.depth % std::size(kDepthColors)], kElementRectAlpha));
            }
            if (anchors && state.anchors[index]) {
                DrawAnchorMark(drawList, frame, *state.anchors[index], false);
            }
        }
    }

    void ViewportUIDebug::DrawHoveredElement(ImDrawList& drawList, const ViewportFrame& frame, bool anchors) const {
        const CanvasState& state = _states[_hoverElement->canvas];
        const UICanvasLayout& layout = state.canvas->GetLayout();
        const UIElement& element = *_hoverElement->element;
        const Rect rect = layout.GetResolved().transform.ToOutput(element.rect.x, element.rect.y, element.rect.w, element.rect.h);
        const auto [min, max] = Corners(frame, rect);
        drawList.AddRectFilled(min, max, WithAlpha(kAnchorColor, kHoveredFillAlpha));
        drawList.AddRect(min, max, WithAlpha(kAnchorColor, kStrongAlpha), 0.0f, 0, kHoveredThickness);
        if (anchors) {
            if (const Optional<AnchorMark> mark = UIPreviewOverlay::AnchorsOf(layout, element)) {
                DrawAnchorMark(drawList, frame, *mark, true);
            }
        }
    }

    void ViewportUIDebug::DrawDesignRect(ImDrawList& drawList, const ViewportFrame& frame, const CanvasState& state) const {
        const Optional<Rect> design = UIPreviewOverlay::DesignRect(state.canvas->GetLayout());
        if (!design) {
            return;
        }
        const auto [min, max] = Corners(frame, *design);
        AddDashedRect(drawList, min, max, kDesignColor);
        drawList.AddText({min.x + kDesignLabelInset, min.y + kDesignLabelInset}, kDesignColor, kDesignLabel);
    }

    void ViewportUIDebug::DrawFindings(ImDrawList& drawList, const ViewportFrame& frame, const CanvasState& state, int64_t hoveredIndex) const {
        for (size_t index = 0; index < state.findingMarks.size(); ++index) {
            const FindingMark& mark = state.findingMarks[index];
            const ImU32 color = kFindingColors[static_cast<size_t>(mark.kind)];
            const auto [min, max] = Corners(frame, mark.rect);
            drawList.AddRectFilled(min, max, WithAlpha(color, kFindingFillAlpha));
            drawList.AddRect(min, max, WithAlpha(color, kFindingLineAlpha), 0.0f, 0, static_cast<int64_t>(index) == hoveredIndex ? kHoveredFindingThickness : kFindingThickness);
        }
    }

    bool ViewportUIDebug::DrawFindingsWindow() {
        bool open = true;
        ImGui::SetNextWindowSize(kFindingsWindowSize, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(kFindingsWindowTitle, &open)) {
            ImGui::End();
            return open;
        }
        bool any = false;
        for (size_t canvas = 0; canvas < _states.size(); ++canvas) {
            CanvasState& state = _states[canvas];
            if (!IsShown(state) || !state.findingsValid) {
                continue;
            }
            any = true;
            const Vector<UILayoutIssue>& issues = state.findings.Get(state.canvas->GetLayout(), _minTouchSize);
            const char* const name = state.name.Empty() ? kUnnamed : state.name.CStr();
            ImGui::PushID(state.node);
            if (ImGui::TreeNodeEx("##canvas", ImGuiTreeNodeFlags_DefaultOpen, "%s (%zu)", name, issues.size())) {
                if (issues.empty()) {
                    ImGui::TextDisabled("No findings");
                }
                for (size_t index = 0; index < issues.size(); ++index) {
                    UIPreviewOverlay::FindingText(issues[index], _findingText);
                    const bool underViewportPointer = _hoverFinding && _hoverFinding->canvas == canvas && _hoverFinding->index == index;
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::Selectable(_findingText.c_str(), underViewportPointer)) {
                        SelectUIElementRequestEvent::Emit(_scene, state.node, issues[index].source);
                    }
                    ImGui::PopID();
                    if (ImGui::IsItemHovered()) {
                        _listHover = HoverFinding{canvas, index};
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (!any) {
            ImGui::TextDisabled("No findings");
        }
        ImGui::End();
        return open;
    }

    bool ViewportUIDebug::ShowFindingTooltip() {
        if (!_hoverFinding) {
            return false;
        }
        CanvasState& state = _states[_hoverFinding->canvas];
        // The cache answers again without validating: nothing changed since Draw()
        const Vector<UILayoutIssue>& issues = state.findings.Get(state.canvas->GetLayout(), _minTouchSize);
        if (_hoverFinding->index >= issues.size()) {
            return false;
        }
        UIPreviewOverlay::FindingText(issues[_hoverFinding->index], _findingText);
        ImGui::SetTooltip("%s", _findingText.c_str());
        return true;
    }

    bool ViewportUIDebug::ShowElementTooltip() {
        if (!_hoverElement) {
            return false;
        }
        const CanvasState& state = _states[_hoverElement->canvas];
        const UICanvasLayout& layout = state.canvas->GetLayout();
        const UIElement& element = *_hoverElement->element;
        UIVariants::Sources(layout.variants, layout.GetResolved().bounds, element.name, _sources);
        UIVariants::DescribeSources({_sources.data(), _sources.size()}, _sourceText);
        const StringView name = element.name.Empty() ? StringView(kUnnamed) : element.name.ToStringView();
        ImGui::SetTooltip("%.*s\nx %g  y %g  w %g  h %g\nstyle: %s", static_cast<int>(name.size()), name.data(), element.rect.x, element.rect.y, element.rect.w, element.rect.h, _sourceText.c_str());
        return true;
    }

}  // namespace BECore
