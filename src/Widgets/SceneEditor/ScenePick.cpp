#include "ScenePick.h"

#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/UI/UIPreviewOverlay.h>
#include <numbers>

namespace BECore::ScenePick {

    namespace {

        constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0f;
        constexpr size_t kNodeComponentsNeeded = 2;  // the transform and something that is drawn or runs

        /// A point turned about (@p centerX, @p centerY) by @p degrees, clockwise on a screen whose y points down
        Vec2 Turn(float x, float y, float centerX, float centerY, float degrees) {
            const float radians = degrees * kDegreesToRadians;
            const float sine = std::sin(radians);
            const float cosine = std::cos(radians);
            const float dx = x - centerX;
            const float dy = y - centerY;
            return {centerX + dx * cosine - dy * sine, centerY + dx * sine + dy * cosine};
        }

        /// The transform of a node that can be picked or outlined: a positive size, and for picking one more component
        IntrusivePtr<TransformComponent> SizedTransform(const SceneNode& node) {
            auto transform = node.GetComponent<TransformComponent>();
            if (!transform || !(transform->_width > 0.0f) || !(transform->_height > 0.0f)) {
                return {};
            }
            return transform;
        }

        struct Best {
            Hit hit;
            int32_t rank = -1;
        };

        void Offer(const Scene& scene, Best& best, SceneNode& node, UIElement* element) {
            const int32_t rank = scene.GetDrawRank(node);
            if (rank > best.rank) {
                best.rank = rank;
                best.hit = {&node, element};
            }
        }

        void Collect(Scene& scene, SceneNode& node, float outputX, float outputY, float sceneX, float sceneY, Best& best) {
            if (node.HasEditorFlag(EditorFlags::Hidden)) {
                return;
            }
            bool picked = false;
            if (auto canvas = node.GetComponent<UICanvasComponent>(); canvas && canvas->_visible && canvas->GetLayout().HasLayout()) {
                const auto [canvasX, canvasY] = canvas->GetLayout().OutputToCanvas(outputX, outputY);
                if (UIElement* element = canvas->GetLayout().PickElement(canvasX, canvasY)) {
                    Offer(scene, best, node, element);
                    picked = true;
                }
            }
            if (!picked && node.GetComponents().size() >= kNodeComponentsNeeded) {
                if (auto transform = SizedTransform(node)) {
                    const Rect rect{transform->_x, transform->_y, transform->_width, transform->_height};
                    if (PointInRotatedRect(rect, transform->_rotation, transform->_pivotX, transform->_pivotY, sceneX, sceneY)) {
                        Offer(scene, best, node, nullptr);
                    }
                }
            }
            for (const auto& child : node.GetChildren()) {
                Collect(scene, *child, outputX, outputY, sceneX, sceneY, best);
            }
        }

        Quad TurnedCorners(const Rect& rect, float rotationDegrees, float pivotX, float pivotY) {
            const float centerX = rect.x + pivotX * rect.w;
            const float centerY = rect.y + pivotY * rect.h;
            return {Turn(rect.x, rect.y, centerX, centerY, rotationDegrees), Turn(rect.x + rect.w, rect.y, centerX, centerY, rotationDegrees),
                    Turn(rect.x + rect.w, rect.y + rect.h, centerX, centerY, rotationDegrees), Turn(rect.x, rect.y + rect.h, centerX, centerY, rotationDegrees)};
        }

        Quad RectCorners(const Rect& rect) {
            return {Vec2{rect.x, rect.y}, Vec2{rect.x + rect.w, rect.y}, Vec2{rect.x + rect.w, rect.y + rect.h}, Vec2{rect.x, rect.y + rect.h}};
        }

        UIElement* FindIn(UIElement& element, const UIElement* wanted) {
            if (&element == wanted) {
                return &element;
            }
            for (const auto& child : element.children) {
                if (UIElement* found = FindIn(*child, wanted)) {
                    return found;
                }
            }
            return nullptr;
        }

    }  // namespace

    bool PointInRotatedRect(const Rect& rect, float rotationDegrees, float pivotX, float pivotY, float x, float y) {
        // Turn the point back instead of the rect forward: the test is then the plain one of an unturned rect
        const Vec2 local = Turn(x, y, rect.x + pivotX * rect.w, rect.y + pivotY * rect.h, -rotationDegrees);
        return local.x >= rect.x && local.y >= rect.y && local.x < rect.x + rect.w && local.y < rect.y + rect.h;
    }

    Hit PickAt(Scene& scene, float outputX, float outputY) {
        Best best;
        Collect(scene, scene.GetRootNode(), outputX, outputY, scene.ScreenToSceneX(outputX), scene.ScreenToSceneY(outputY), best);
        return best.hit;
    }

    Outlines SelectionOutlines(const Scene& scene, const SceneNode& node) {
        Outlines outlines;
        if (auto transform = SizedTransform(node)) {
            const ViewTransform& view = scene.GetViewTransform();
            Quad quad = TurnedCorners({transform->_x, transform->_y, transform->_width, transform->_height}, transform->_rotation, transform->_pivotX, transform->_pivotY);
            for (Vec2& corner : quad) {
                const Rect output = view.ToOutput(corner.x, corner.y, 0.0f, 0.0f);
                corner = {output.x, output.y};
            }
            outlines.quads[outlines.count++] = quad;
        }
        if (auto canvas = node.GetComponent<UICanvasComponent>(); canvas && canvas->GetLayout().HasLayout()) {
            outlines.quads[outlines.count++] = RectCorners(UIPreviewOverlay::CanvasBounds(canvas->GetLayout().GetResolved()));
        }
        return outlines;
    }

    UIElement* FindElement(UICanvasComponent& canvas, const UIElement* wanted) {
        return wanted ? FindIn(canvas.GetRoot(), wanted) : nullptr;
    }

}  // namespace BECore::ScenePick
