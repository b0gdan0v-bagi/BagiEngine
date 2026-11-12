#include "ViewportGizmos.h"

#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <Widgets/SceneEditor/ScenePick.h>
#include <Widgets/SceneEditor/SceneSelection.h>
#include <Widgets/Viewport/ViewportInput.h>

namespace BECore {

    namespace {

        constexpr int kLeftButton = 0;
        constexpr int kMiddleButton = 2;

        constexpr float kRubberBandFillAlpha = 0.18f;
        constexpr float kRubberBandThickness = 1.0f;

        ImVec2 ToImVec(Vec2 point) {
            return {point.x, point.y};
        }

    }  // namespace

    ViewportFrame MakeViewportFrame(const ViewportCamera& camera, const ImVec2& panelMin, const ImVec2& panelSize, const ImVec2& targetSize, float baseZoom) {
        const Rect panel{panelMin.x, panelMin.y, panelSize.x, panelSize.y};
        const Vec2 origin = camera.ToScreen({0.0f, 0.0f}, panel, baseZoom);
        ViewportFrame frame;
        frame.imageMin = panelMin;
        frame.imageMax = {panelMin.x + panelSize.x, panelMin.y + panelSize.y};
        frame.scale = camera.Scale(baseZoom);
        frame.origin = ToImVec(origin);
        frame.end = {origin.x + targetSize.x * frame.scale, origin.y + targetSize.y * frame.scale};
        return frame;
    }

    bool SelectionBounds(Scene& scene, eastl::span<SceneNode* const> selected, Rect& bounds) {
        Vec2 low{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        Vec2 high{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        bool any = false;
        for (SceneNode* const node : selected) {
            const SceneNode* const alive = HierarchyModel::FindNode(scene.GetRootNode(), node);
            if (!alive) {
                continue;
            }
            const ScenePick::Outlines outlines = ScenePick::SelectionOutlines(scene, *alive);
            for (size_t i = 0; i < outlines.count; ++i) {
                for (const Vec2& corner : outlines.quads[i]) {
                    low = {std::min(low.x, corner.x), std::min(low.y, corner.y)};
                    high = {std::max(high.x, corner.x), std::max(high.y, corner.y)};
                    any = true;
                }
            }
        }
        if (any) {
            bounds = {low.x, low.y, high.x - low.x, high.y - low.y};
        }
        return any;
    }

    bool ViewportKeyPressed(ImGuiKey key) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput || io.KeyCtrl || io.KeyAlt || io.KeyShift || io.KeySuper) {
            return false;
        }
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
            return false;
        }
        return ImGui::IsKeyPressed(key, false);
    }

    bool ViewportCameraInput::Update(ViewportCamera& camera, const Rect& panel, float baseZoom, bool hovered, bool enabled) {
        const ImGuiIO& io = ImGui::GetIO();
        if (_panning && (!enabled || !io.MouseDown[_button])) {
            _panning = false;
        }
        if (_panning) {
            camera.PanBy({io.MouseDelta.x, io.MouseDelta.y}, baseZoom);
            return false;
        }
        if (!enabled || !hovered) {
            return false;
        }
        if (ImGui::IsMouseClicked(kMiddleButton)) {
            _panning = true;
            _button = kMiddleButton;
            return true;
        }
        if (ImGui::IsMouseClicked(kLeftButton) && ImGui::IsKeyDown(ImGuiKey_Space)) {
            _panning = true;
            _button = kLeftButton;
            return true;
        }
        if (io.MouseWheel != 0.0f) {
            camera.ZoomAt(ViewportCamera::WheelFactor(io.MouseWheel), {io.MousePos.x, io.MousePos.y}, panel, baseZoom);
        }
        return false;
    }

    void ViewportBoxSelect::Update(const ViewportFrame& frame, Scene* scene, bool enabled, bool hovered, bool pressTaken) {
        const ImGuiIO& io = ImGui::GetIO();
        if (_active) {
            if (!enabled || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                _active = false;
                return;
            }
            const bool isBox = IsBoxSelectDrag(_startScreen.x, _startScreen.y, io.MousePos.x, io.MousePos.y);
            if (!io.MouseDown[kLeftButton]) {
                _active = false;
                if (isBox) {
                    const Vec2 end = frame.ToTarget(io.MousePos);
                    ViewportBoxSelectEvent::Emit(_startTarget.x, _startTarget.y, end.x, end.y, SelectionModifiersOf(io.KeyCtrl, io.KeyShift));
                }
                return;
            }
            if (isBox) {
                const ImGuiPalette& palette = GetImGuiPalette();
                ImVec4 fill = palette.accent;
                fill.w = kRubberBandFillAlpha;
                ImDrawList* const drawList = ImGui::GetWindowDrawList();
                drawList->PushClipRect(frame.imageMin, frame.imageMax, true);
                drawList->AddRectFilled(_startScreen, io.MousePos, ImGui::ColorConvertFloat4ToU32(fill));
                drawList->AddRect(_startScreen, io.MousePos, ImGui::ColorConvertFloat4ToU32(palette.accent), 0.0f, 0, kRubberBandThickness);
                drawList->PopClipRect();
            }
            return;
        }
        if (!enabled || !hovered || pressTaken || !scene || !ImGui::IsMouseClicked(kLeftButton)) {
            return;
        }
        // Only a press on empty ground starts a band: a press on a node is a click on it
        const Vec2 target = frame.ToTarget(io.MousePos);
        if (ScenePick::PickAt(*scene, target.x, target.y).node == nullptr) {
            _active = true;
            _startScreen = io.MousePos;
            _startTarget = target;
        }
    }

}  // namespace BECore
