#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIPreview.h>
#include <BECore/UI/UIPreviewOverlay.h>
#include <BECore/UI/UIVariant.h>
#include <BECore/Utils/FunctionRef.h>
#include <Widgets/Viewport/ViewportGizmos.h>
#include <Widgets/Viewport/ViewportToolbarState.h>
#include <cstdint>
#include <imgui.h>

namespace BECore {

    class Scene;
    class SceneNode;

    /// A dashed rectangle outline between two screen corners, one pixel wide
    void AddDashedRect(ImDrawList& drawList, const ImVec2& min, const ImVec2& max, ImU32 color);

    /**
     * @brief The UI debug overlays of the viewport: the rect of every element, the anchors and pivots, the design rectangle of the canvas scaler and the findings of the layout validator, drawn over
     *        the image from the marks of UIPreviewOverlay (output pixels), and the tooltips that go with them.
     *
     * Needs an ImGui frame in progress. Each canvas has a runtime switch (not stored, kept by the caller) for all four overlays. The marks of a canvas are made again only when its layout ran again
     * (and the findings when the minimum touch size moved), so a steady frame validates, collects and allocates nothing. The canvases are held by pointer between calls; Release() lets them go.
     */
    class ViewportUIDebug {
    public:
        /// The canvases of the subtree of @p node in the order of the scene, without the subtree of a node hidden in the editor (it draws nothing, so it gets no overlay)
        static void CollectCanvases(const SceneNode& node, Vector<IntrusivePtr<UICanvasComponent>>& out);

        /// True when one of the four overlays is on; with none, nothing is collected or validated
        [[nodiscard]] static bool AnyOn(const PreviewOverlays& overlays);

        /// Finds the canvases of @p scene (visible or not: a hidden canvas is listed for its switch and draws nothing). The switches are the caller's: @p isEnabled
        /// says whether the overlays of a canvas's node are on
        void Collect(Scene* scene, FunctionRef<bool(const SceneNode*)> isEnabled);

        /// The canvases of the last Collect(), in the order of the scene, for their switches
        [[nodiscard]] Span<const CanvasSwitchRow> GetCanvasRows() const {
            return {_canvasRows.data(), _canvasRows.size()};
        }

        /// Lets the canvases go (the overlays are all off, or the scene is gone)
        void Release();

        /**
         * @brief Draws the overlays that are on over the image, inside the clip rect the caller pushed. Finds what is under the pointer for the tooltips (only when @p hovered).
         *
         * @p mouse is the screen position of the pointer. The findings validate with UISettings::minTouchSize at density 1.
         */
        void Draw(ImDrawList& drawList, const ViewportFrame& frame, const PreviewOverlays& overlays, bool hovered, const ImVec2& mouse);

        /**
         * @brief The "UI Findings" window: per enabled canvas of the last Collect() a header with the count and one row per finding, from the findings of the last Draw() (no second validation).
         *
         * The row under the pointer is outlined in the viewport on the next Draw(), the row of the finding under the pointer in the viewport is highlighted; a click on a row emits
         * SelectUIElementRequestEvent with the node of the canvas and the element at fault. Needs an ImGui frame outside the viewport's window.
         * @return false when the user closed the window (the caller clears the overlay flag)
         */
        bool DrawFindingsWindow();

        /// The tooltip of the finding under the pointer at the last Draw(); false when there is none. A finding wins over every other tooltip of the viewport.
        bool ShowFindingTooltip();

        /// The tooltip of the element under the pointer at the last Draw() (name, rect in canvas units, style source); false when there is none
        bool ShowElementTooltip();

    private:
        // What the overlays keep for one canvas: the marks of its last layout, which are in output pixels and do not move with the camera
        struct CanvasState {
            IntrusivePtr<UICanvasComponent> canvas;
            const SceneNode* node = nullptr;  // of the last Collect(), for the switch: the canvas may outlive its node, so never ask the canvas
            PoolString name;
            bool enabled = true;
            uint32_t elementRuns = 0;  // GetLayoutRuns() the element and anchor marks were made for
            bool elementsValid = false;
            Vector<ElementMark> elements;
            Vector<Optional<AnchorMark>> anchors;  // parallel to `elements`
            UIFindingsCache findings;
            uint32_t findingRuns = 0;  // UIFindingsCache::GetValidationRuns() the marks were made for
            bool findingsValid = false;
            Vector<FindingMark> findingMarks;
        };

        // A canvas the overlays show: switched on, visible and laid out. A canvas that is not keeps its old marks but nothing draws, hovers or lists them.
        [[nodiscard]] static bool IsShown(const CanvasState& state);

        // Where the pointer is: a canvas of _states and an index into its findings, or an element picked in a canvas
        struct HoverFinding {
            size_t canvas = 0;
            size_t index = 0;
        };
        struct HoverElement {
            size_t canvas = 0;
            const UIElement* element = nullptr;
        };

        void RefreshElements(CanvasState& state) const;
        void RefreshFindings(CanvasState& state, float minTouchSize) const;
        void FindHoverElement(const ViewportFrame& frame, const ImVec2& mouse);
        void DrawElements(ImDrawList& drawList, const ViewportFrame& frame, const CanvasState& state, bool rects, bool anchors) const;
        void DrawHoveredElement(ImDrawList& drawList, const ViewportFrame& frame, bool anchors) const;
        void DrawDesignRect(ImDrawList& drawList, const ViewportFrame& frame, const CanvasState& state) const;
        void DrawFindings(ImDrawList& drawList, const ViewportFrame& frame, const CanvasState& state, int64_t hoveredIndex) const;

        Vector<IntrusivePtr<UICanvasComponent>> _collected;  // the scratch of Collect(), kept for its capacity
        Vector<CanvasState> _states;                         // in the order of the scene; the last is drawn on top
        Vector<CanvasSwitchRow> _canvasRows;                 // the canvases of the last Collect(), kept for its capacity
        const Scene* _scene = nullptr;                       // of the last Collect(), for the request a click in the findings window sends
        float _minTouchSize = 0.0f;                          // of the last Draw(), the key of the findings the tooltip asks for
        Optional<HoverElement> _hoverElement;
        Optional<HoverFinding> _hoverFinding;
        Optional<HoverFinding> _listHover;  // the row of the findings window under the pointer, outlined by the next Draw()
        Vector<UIVariantSource> _sources;   // the scratch of the element tooltip
        String _sourceText;
        String _findingText;
    };

}  // namespace BECore
