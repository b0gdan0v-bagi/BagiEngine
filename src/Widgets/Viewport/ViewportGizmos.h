#pragma once

#include <BECore/Math/Vec2.h>
#include <Widgets/Viewport/GizmoDrag.h>
#include <Widgets/Viewport/ViewportCamera.h>
#include <Widgets/Viewport/ViewportEvents.h>
#include <imgui.h>

namespace BECore {

    class Scene;
    class SceneNode;
    class TransformComponent;
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

    /// The fields of @p transform as a gizmo item, and back
    [[nodiscard]] Gizmo::Item ItemOf(const TransformComponent& transform);
    void ApplyItem(TransformComponent& transform, const Gizmo::Item& item);

    /// True when the gizmo may move @p node: it has a TransformComponent and the editor has not locked it. A canvas-only node has no gizmo
    [[nodiscard]] bool IsMovable(const SceneNode& node);

    /// The nodes of @p selected that are alive in @p scene (found in its tree, never dereferenced before) and movable, with their transforms as items, in the order of @p selected. The vectors are
    /// cleared first and keep their capacity
    void CollectMovable(Scene& scene, eastl::span<SceneNode* const> selected, eastl::vector<SceneNode*>& nodes, eastl::vector<Gizmo::Item>& items);

    /// True when every node is found in the tree of @p scene
    [[nodiscard]] bool AllAlive(Scene& scene, eastl::span<SceneNode* const> nodes);

    /// Writes @p items into the TransformComponents of @p nodes (the same order). Nothing is written, and false returned, when a node is not alive in @p scene or has no transform, or the sizes differ
    bool WriteItems(Scene& scene, eastl::span<SceneNode* const> nodes, eastl::span<const Gizmo::Item> items);

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

    /// What ViewportGizmos::Update() needs to know about this frame
    struct ViewportGizmoContext {
        Scene* scene = nullptr;                   ///< the active scene
        eastl::span<SceneNode* const> selection;  ///< the selected nodes, the primary last
        const ViewportFrame* frame = nullptr;
        bool editMode = false;  ///< the input mode is Edit: a drag in progress ends cleanly when it is not
        bool enabled = false;   ///< the Edit target owns the pointer and no pan is going: the gizmo is drawn and a press may grab a handle
        bool hovered = false;   ///< the pointer is over the image
    };

    /**
     * @brief One drag of the gizmo over live nodes: the writes into the TransformComponents and NodeTransformEditEvent. No ImGui.
     *
     * Begin() takes the nodes and their items (CollectMovable) and tells @c Begin; Update() writes the result of the pointer into the nodes and tells @c Change in a call
     * where something differs from the last one; Finish() ends at the release with a @c Commit when something moved and with a @c Cancel when nothing did, so a subscriber always
     * gets an end; Abort() gives the nodes their start values back and tells @c Cancel. The session keeps raw node pointers and a node is never touched before it is found in the tree
     * of the active scene, so a node deleted by anything else ends the drag (CanContinue()) instead of being written to; a replaced scene has nothing left to give back, and its
     * @c Cancel carries no edits.
     */
    class GizmoEditSession {
    public:
        /// Starts the drag and tells @c Begin. False (nothing started) for no nodes, mismatched sizes or Handle::None
        bool Begin(Scene& scene, eastl::span<SceneNode* const> nodes, eastl::span<const Gizmo::Item> items, Gizmo::Tool tool, Gizmo::Space space, Gizmo::Handle handle, Vec2 point,
                   const Gizmo::SnapSettings& snap);

        /// True while the drag can go on in @p activeScene: it is the scene of the drag and every node is still in its tree
        [[nodiscard]] bool CanContinue(Scene* activeScene) const;

        /// The pointer is at @p point (scene units); @p snapModifier flips the snap switches, @p keepAspect keeps the aspect of a scale. Writes the nodes and tells @c Change when the result differs
        /// from the last
        void Update(Scene& scene, Vec2 point, bool snapModifier, bool keepAspect);

        /// The release: @c Commit when a node differs from its start, otherwise a @c Cancel
        void Finish(Scene& scene);

        /// Gives back the start values (to the nodes that are still in the tree of @p activeScene, when it is the scene of the drag) and tells @c Cancel. Nothing for no drag
        void Abort(Scene* activeScene);

        [[nodiscard]] bool IsActive() const {
            return _drag.IsActive();
        }

        [[nodiscard]] Gizmo::Handle GetHandle() const {
            return _handle;
        }

        /// The items of the last Update() (the start values before the first one)
        [[nodiscard]] eastl::span<const Gizmo::Item> Current() const {
            return _drag.Current();
        }

    private:
        void Emit(NodeTransformEditPhase phase) const;

        Gizmo::GizmoDrag _drag;
        Gizmo::Handle _handle = Gizmo::Handle::None;
        const Scene* _scene = nullptr;  // compared with the active scene, never dereferenced
        eastl::vector<SceneNode*> _nodes;
        eastl::vector<NodeTransformEdit> _edits;  // the nodes with their start and current values
    };

    /**
     * @brief The gizmo of the viewport over the selected nodes: the toolbar, the keys, the drawing, and the drag that moves, turns or scales them.
     *
     * The maths are Gizmo::GizmoDrag and GizmoGeometry, the writes and the events are GizmoEditSession; this is the ImGui layer: it hit-tests, draws, starts a drag on a press and
     * ends it at the release, on Esc, or when the session cannot go on (another scene, a node that is gone, the input mode left Edit, AbortDrag()).
     */
    class ViewportGizmos {
    public:
        /// The second toolbar row: the tool group, the World / Local toggle and the Snap popup
        void DrawToolbar();

        /// Reads W / E / R / X (tool and space) while the Viewport window has the keys
        void ReadKeys();

        /// One frame: continues or ends a drag, otherwise hit-tests, draws, and starts a drag on a press. @return true when the left press of this frame was taken (it is not an edit click)
        bool Update(const ViewportGizmoContext& context);

        /// Ends a drag in progress as a Cancel (the nodes get their start values back when @p activeScene is still the scene of the drag and they are alive). Safe to call with no drag
        void AbortDrag(Scene* activeScene);

        [[nodiscard]] bool IsDragging() const {
            return _session.IsActive();
        }

        void SetTool(Gizmo::Tool tool) {
            _tool = tool;
        }

        void ToggleSpace() {
            _space = _space == Gizmo::Space::World ? Gizmo::Space::Local : Gizmo::Space::World;
        }

        /// All three snap switches off when any is on, otherwise all on (the steps stay)
        void ToggleSnap() {
            const bool anyOn = _snap.position || _snap.angle || _snap.scale;
            _snap.position = _snap.angle = _snap.scale = !anyOn;
        }

        [[nodiscard]] Gizmo::Tool GetTool() const {
            return _tool;
        }

        [[nodiscard]] Gizmo::Space GetSpace() const {
            return _space;
        }

    private:
        void Draw(const ViewportGizmoContext& context, const Gizmo::Frame& gizmoFrame, float unitsPerScreenPixel) const;
        void DrawToolButton(const char* icon, const char* tooltip, const char* id, Gizmo::Tool tool);
        void DrawSnapPopup();

        Gizmo::Tool _tool = Gizmo::Tool::Translate;
        Gizmo::Space _space = Gizmo::Space::World;
        Gizmo::SnapSettings _snap;
        GizmoEditSession _session;
        Gizmo::Handle _hover = Gizmo::Handle::None;
        eastl::vector<SceneNode*> _nodes;  // the movable selected nodes of the last hit test
        eastl::vector<Gizmo::Item> _items;
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
