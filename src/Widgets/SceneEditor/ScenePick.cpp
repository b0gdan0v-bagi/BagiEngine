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

        /// The node and its subtree in tree order, skipping a hidden node with everything below it (the draw system does the same)
        template <typename Visitor>
        void VisitVisible(SceneNode& node, Visitor& visit) {
            if (node.HasEditorFlag(EditorFlags::Hidden)) {
                return;
            }
            visit(node);
            for (const auto& child : node.GetChildren()) {
                VisitVisible(*child, visit);
            }
        }

        /// The transform of a node whose own rect can be picked: a positive size and one more component
        IntrusivePtr<TransformComponent> PickableTransform(const SceneNode& node) {
            if (node.GetComponents().size() < kNodeComponentsNeeded) {
                return {};
            }
            return SizedTransform(node);
        }

        /// The canvas of a node that is drawn and has been laid out, or null
        IntrusivePtr<UICanvasComponent> LaidOutCanvas(const SceneNode& node) {
            auto canvas = node.GetComponent<UICanvasComponent>();
            return canvas && canvas->_visible && canvas->GetLayout().HasLayout() ? canvas : IntrusivePtr<UICanvasComponent>{};
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

        /// The rect of @p transform with its rotation applied, taken to output pixels by the view of @p scene
        Quad OutputQuad(const Scene& scene, const TransformComponent& transform) {
            const ViewTransform& view = scene.GetViewTransform();
            Quad quad = TurnedCorners({transform._x, transform._y, transform._width, transform._height}, transform._rotation, transform._pivotX, transform._pivotY);
            for (Vec2& corner : quad) {
                const Rect output = view.ToOutput(corner.x, corner.y, 0.0f, 0.0f);
                corner = {output.x, output.y};
            }
            return quad;
        }

        struct Box {
            float x0, y0, x1, y1;  ///< x0 <= x1 and y0 <= y1
        };

        /// True when the convex @p quad and @p box share a point (touching edges count). The separating axis test: the axes of the box, then the two edge directions of the quad
        bool QuadTouchesBox(const Quad& quad, const Box& box) {
            float minX = quad[0].x;
            float maxX = quad[0].x;
            float minY = quad[0].y;
            float maxY = quad[0].y;
            for (const Vec2& corner : quad) {
                minX = std::min(minX, corner.x);
                maxX = std::max(maxX, corner.x);
                minY = std::min(minY, corner.y);
                maxY = std::max(maxY, corner.y);
            }
            if (maxX < box.x0 || minX > box.x1 || maxY < box.y0 || minY > box.y1) {
                return false;
            }
            const eastl::array<Vec2, 4> boxCorners{Vec2{box.x0, box.y0}, Vec2{box.x1, box.y0}, Vec2{box.x1, box.y1}, Vec2{box.x0, box.y1}};
            constexpr size_t kEdgeAxes = 2;  // a quad is a parallelogram: its first two edges give both normals
            for (size_t edge = 0; edge < kEdgeAxes; ++edge) {
                const Vec2 direction{quad[edge + 1].x - quad[edge].x, quad[edge + 1].y - quad[edge].y};
                const Vec2 normal{-direction.y, direction.x};
                float quadMin = std::numeric_limits<float>::max();
                float quadMax = std::numeric_limits<float>::lowest();
                for (const Vec2& corner : quad) {
                    const float along = corner.x * normal.x + corner.y * normal.y;
                    quadMin = std::min(quadMin, along);
                    quadMax = std::max(quadMax, along);
                }
                float boxMin = std::numeric_limits<float>::max();
                float boxMax = std::numeric_limits<float>::lowest();
                for (const Vec2& corner : boxCorners) {
                    const float along = corner.x * normal.x + corner.y * normal.y;
                    boxMin = std::min(boxMin, along);
                    boxMax = std::max(boxMax, along);
                }
                if (boxMax < quadMin || boxMin > quadMax) {
                    return false;
                }
            }
            return true;
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
        const float sceneX = scene.ScreenToSceneX(outputX);
        const float sceneY = scene.ScreenToSceneY(outputY);
        auto offer = [&](SceneNode& node) {
            bool picked = false;
            if (auto canvas = LaidOutCanvas(node)) {
                const auto [canvasX, canvasY] = canvas->GetLayout().OutputToCanvas(outputX, outputY);
                if (UIElement* element = canvas->GetLayout().PickElement(canvasX, canvasY)) {
                    Offer(scene, best, node, element);
                    picked = true;
                }
            }
            if (!picked) {
                if (auto transform = PickableTransform(node)) {
                    const Rect rect{transform->_x, transform->_y, transform->_width, transform->_height};
                    if (PointInRotatedRect(rect, transform->_rotation, transform->_pivotX, transform->_pivotY, sceneX, sceneY)) {
                        Offer(scene, best, node, nullptr);
                    }
                }
            }
        };
        VisitVisible(scene.GetRootNode(), offer);
        return best.hit;
    }

    void PickInRect(Scene& scene, float x0, float y0, float x1, float y1, eastl::vector<SceneNode*>& out) {
        out.clear();
        const Box box{std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
        auto collect = [&](SceneNode& node) {
            bool touched = false;
            if (auto canvas = LaidOutCanvas(node)) {
                touched = QuadTouchesBox(RectCorners(UIPreviewOverlay::CanvasBounds(canvas->GetLayout().GetResolved())), box);
            }
            if (!touched) {
                if (auto transform = PickableTransform(node)) {
                    touched = QuadTouchesBox(OutputQuad(scene, *transform), box);
                }
            }
            if (touched) {
                out.push_back(&node);
            }
        };
        VisitVisible(scene.GetRootNode(), collect);
    }

    Outlines SelectionOutlines(const Scene& scene, const SceneNode& node) {
        Outlines outlines;
        if (auto transform = SizedTransform(node)) {
            outlines.quads[outlines.count++] = OutputQuad(scene, *transform);
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
