#pragma once

#include <BECore/Math/Vec2.h>
#include <Widgets/Viewport/ViewportCamera.h>
#include <Widgets/Viewport/ViewportEvents.h>
#include <imgui.h>

namespace BECore {

    class Scene;
    class SceneNode;
    struct ViewTransform;

    /**
     * @brief Where the viewport's image is on the screen this frame, with the editor camera applied.
     *
     * Everything drawn or read in target pixels (the pixels of the render output) goes through it. @c imageMin and @c imageMax are the panel the unzoomed image takes: the clip rect
     * of everything the viewport draws and the hit area of the image. The target itself is drawn from @c origin to @c end.
     */
    struct ViewportFrame {
        ImVec2 imageMin;
        ImVec2 imageMax;
        ImVec2 origin;       ///< the screen position of target pixel (0, 0)
        ImVec2 end;          ///< the screen position of the far corner of the target
        float scale = 1.0f;  ///< screen units per target pixel

        [[nodiscard]] ImVec2 ToScreen(Vec2 target) const {
            return {origin.x + target.x * scale, origin.y + target.y * scale};
        }

        [[nodiscard]] Vec2 ToTarget(const ImVec2& screen) const {
            return {(screen.x - origin.x) / scale, (screen.y - origin.y) / scale};
        }
    };

    /// The frame of the image when @p camera looks at it: the panel is at @p panelMin and @p panelSize on the screen, the target is @p targetSize pixels and fits the panel at @p baseZoom
    [[nodiscard]] ViewportFrame MakeViewportFrame(const ViewportCamera& camera, const ImVec2& panelMin, const ImVec2& panelSize, const ImVec2& targetSize, float baseZoom);

    /// The bounds, in target pixels, of the outlines (ScenePick::SelectionOutlines) of the nodes of @p selected that are alive in @p scene; false when there is none
    [[nodiscard]] bool SelectionBounds(Scene& scene, eastl::span<SceneNode* const> selected, Rect& bounds);

    /// True when @p key went down in this frame, the Viewport window is focused or hovered, and no text is being typed or modifier key held (a bare letter is never a hotkey of the command table, so
    /// the viewport reads it itself)
    [[nodiscard]] bool ViewportKeyPressed(ImGuiKey key);

    /**
     * @brief The pan of the editor camera by the pointer: the middle button, or the left one with Space held. The wheel zooms about the pointer.
     *
     * A pan that has begun follows the button to its release even when the pointer leaves the image. ImGui only.
     */
    class ViewportCameraInput {
    public:
        /// One frame. @p enabled is false unless the Edit target owns the pointer. @return true on the frame a pan began: that press is not an edit click
        bool Update(ViewportCamera& camera, const Rect& panel, float baseZoom, bool hovered, bool enabled);

        [[nodiscard]] bool IsPanning() const {
            return _panning;
        }

        void Cancel() {
            _panning = false;
        }

    private:
        bool _panning = false;
        int _button = 0;
    };

    /// The rubber band: a left drag that began on empty ground of the image ends in a ViewportBoxSelectEvent. The press itself already went out as an edit click (it cleared the selection), so a drag
    /// shorter than kBoxSelectMinDrag is just that click
    class ViewportBoxSelect {
    public:
        /// One frame. @p enabled is true while the Edit target owns the pointer and no pan or gizmo drag is going; @p pressTaken: a gizmo took this frame's press
        void Update(const ViewportFrame& frame, Scene* scene, bool enabled, bool hovered, bool pressTaken);

        void Cancel() {
            _active = false;
        }

        [[nodiscard]] bool IsActive() const {
            return _active;
        }

    private:
        bool _active = false;
        ImVec2 _startScreen;
        Vec2 _startTarget;
    };

}  // namespace BECore
