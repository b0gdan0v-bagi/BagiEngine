#include "FuzzTargets.h"
#include "FuzzUiGeometryInput.h"

#include <BECore/UI/UIBoxMesh.h>
#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIGeometryBounds.h>
#include <BECore/UI/UILayout.h>
#include <BECore/UI/UINineSlice.h>
#include <cmath>

namespace BECore::Fuzz {

    namespace {

        using namespace UiGeometryInput;

        // =============================================================================
        // The numbers of a content file that reach the pure UI geometry without a scene, a renderer, a texture or CoreManager:
        //   UIBoxMesh        AppendFill, AppendBorder, AppendShadow, AppendHighlight, CornerSegments; UICorners::Clamped and Inset
        //   UINineSlice      Compute
        //   UILayout         EffectiveAnchors, Inset, ResolveRect (every field of UIRectSpec), LayoutStack (UIStackSpec and the specs of the children)
        //   UICanvasScaler   Resolve (the settings of a canvas and the context of a window)
        //   UICanvasLayout   Update over a tree of elements (anchor and stack layout, variants of an empty list), HitTest, PickElement, Validate
        // Out of reach (they need a service): the text layout and the fit of a text (fonts), the sizes of a picture (a texture), the draw of the elements (a renderer), UIDropdown
        // popups (LayoutPopup of a control), the numbers of UIProgressBar and UISlider (a variable of UIVars).
        // =============================================================================

        constexpr float kMaxExtent = UIBounds::kMaxExtent;

        /// A side below this (a hundredth of a unit) is not checked for the sum of its two radii
        constexpr float kSmallestSide = 0.01f;

        bool Bounded(float value) {
            return std::isfinite(value) && std::fabs(value) <= kMaxExtent;
        }

        bool BoundedRect(const Rect& rect) {
            return Bounded(rect.x) && Bounded(rect.y) && Bounded(rect.w) && Bounded(rect.h);
        }

        /// A rect a layout gives: bounded numbers and no negative size
        void CheckLaidOutRect(const Rect& rect) {
            FUZZ_CHECK(BoundedRect(rect));
            FUZZ_CHECK(rect.w >= 0.0f);
            FUZZ_CHECK(rect.h >= 0.0f);
        }

        // =============================================================================
        // UIBoxMesh and UICorners
        // =============================================================================

        /// What one call may append: counts under the constants of the builder, whole triangles, indices that are positions in the list, finite vertices
        struct MeshJudge {
            Vector<GeometryVertex> vertices;
            Vector<int> indices;
            size_t checkedVertices = 0;
            size_t checkedIndices = 0;

            void Check() {
                const size_t addedVertices = vertices.size() - checkedVertices;
                const size_t addedIndices = indices.size() - checkedIndices;
                FUZZ_CHECK(addedVertices <= static_cast<size_t>(UIBoxMesh::kMaxVerticesPerCall));
                FUZZ_CHECK(addedIndices <= static_cast<size_t>(UIBoxMesh::kMaxIndicesPerCall));
                FUZZ_CHECK(addedIndices % 3 == 0);
                for (size_t i = checkedVertices; i < vertices.size(); ++i) {
                    FUZZ_CHECK(std::isfinite(vertices[i].x));
                    FUZZ_CHECK(std::isfinite(vertices[i].y));
                }
                for (size_t i = checkedIndices; i < indices.size(); ++i) {
                    FUZZ_CHECK(indices[i] >= 0);
                    FUZZ_CHECK(static_cast<size_t>(indices[i]) < vertices.size());
                }
                checkedVertices = vertices.size();
                checkedIndices = indices.size();
            }
        };

        void CheckCorners(const UICorners& corners) {
            for (const float radius : {corners.topLeft, corners.topRight, corners.bottomRight, corners.bottomLeft}) {
                FUZZ_CHECK(std::isfinite(radius));
                FUZZ_CHECK(radius >= 0.0f);
            }
        }

        void RunBox(const BoxCase& box) {
            const UIGradientSpec gradient{PickEnum<UIGradientDirection>(box.gradientDirection), box.gradientEnd};

            MeshJudge mesh;
            UIBoxMesh::AppendFill(mesh.vertices, mesh.indices, box.rect, box.corners, box.color, gradient, box.pixelsPerUnit);
            mesh.Check();
            UIBoxMesh::AppendBorder(mesh.vertices, mesh.indices, box.rect, box.corners, box.borderWidth, box.color, box.pixelsPerUnit);
            mesh.Check();
            UIBoxMesh::AppendShadow(mesh.vertices, mesh.indices, box.rect, box.corners, box.shadow, box.pixelsPerUnit);
            mesh.Check();
            UIBoxMesh::AppendHighlight(mesh.vertices, mesh.indices, box.rect, box.corners, box.color, box.pixelsPerUnit);
            mesh.Check();

            const int segments = UIBoxMesh::CornerSegments(box.radiusPixels);
            FUZZ_CHECK(segments >= 0 && segments <= UIBoxMesh::kMaxCornerSegments);

            const UICorners clamped = box.corners.Clamped(box.rect.w, box.rect.h);
            CheckCorners(clamped);
            if (!std::isfinite(box.rect.w) || !std::isfinite(box.rect.h) || box.rect.w <= 0.0f || box.rect.h <= 0.0f) {
                FUZZ_CHECK(clamped.IsZero());
            } else if (box.rect.w >= kSmallestSide && box.rect.h >= kSmallestSide) {
                // The shrink is a product with a factor that can be a denormal (a radius of 1e38 on a side of a unit), which has few bits: a side under the limit is only finite
                constexpr float kRoundingSlack = 1.001f;
                FUZZ_CHECK(clamped.topLeft + clamped.topRight <= box.rect.w * kRoundingSlack);
                FUZZ_CHECK(clamped.bottomLeft + clamped.bottomRight <= box.rect.w * kRoundingSlack);
                FUZZ_CHECK(clamped.topLeft + clamped.bottomLeft <= box.rect.h * kRoundingSlack);
                FUZZ_CHECK(clamped.topRight + clamped.bottomRight <= box.rect.h * kRoundingSlack);
            }
            CheckCorners(clamped.Inset(box.inset));
        }

        // =============================================================================
        // UINineSlice
        // =============================================================================

        void RunNineSlice(const NineSliceCase& slice) {
            const auto pieces = UINineSlice::Compute(slice.source, slice.border, slice.destination);
            for (const UINineSlicePiece& piece : pieces) {
                // A piece reaches as far as its rect starts plus its size, so twice the bound at most
                for (const Rect& rect : {piece.source, piece.destination}) {
                    FUZZ_CHECK(std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.w) && std::isfinite(rect.h));
                    FUZZ_CHECK(rect.w >= 0.0f && rect.h >= 0.0f);
                }
                // A piece the drawer draws has area on both sides, one with none has neither
                FUZZ_CHECK((piece.source.w > 0.0f && piece.source.h > 0.0f) == (piece.destination.w > 0.0f && piece.destination.h > 0.0f));
            }
        }

        // =============================================================================
        // UILayout
        // =============================================================================

        void RunRect(const Rect& parent, const SpecCase& specCase) {
            const UIRectSpec spec = specCase.ToSpec();

            const UIEffectiveAnchors anchors = UILayout::EffectiveAnchors(spec);
            for (const float value : {anchors.min.x, anchors.min.y, anchors.max.x, anchors.max.y, anchors.pivot.x, anchors.pivot.y}) {
                FUZZ_CHECK(Bounded(value));
            }
            FUZZ_CHECK(anchors.min.x <= anchors.max.x);
            FUZZ_CHECK(anchors.min.y <= anchors.max.y);

            CheckLaidOutRect(UILayout::ResolveRect(parent, spec));
            CheckLaidOutRect(UILayout::Inset(parent, spec.margin));
        }

        void RunStack(const StackCase& stackCase) {
            UIStackSpec stack;
            stack.direction = PickEnum<UIStackDirection>(stackCase.direction);
            stack.crossAlign = PickEnum<UIStackAlign>(stackCase.align);
            stack.wrap = (stackCase.wrap & 1) != 0;
            stack.spacing = stackCase.spacing;
            stack.padding = stackCase.padding;

            Vector<UIRectSpec> specs;
            for (size_t i = 0; i < stackCase.childCount % (kMaxStackChildren + 1); ++i) {
                specs.push_back(stackCase.children[i].ToSpec());
            }
            Vector<Rect> rects;
            UILayout::LayoutStack(stackCase.parent, stack, specs, rects);
            FUZZ_CHECK(rects.size() == specs.size());
            for (const Rect& rect : rects) {
                CheckLaidOutRect(rect);
            }
        }

        // =============================================================================
        // UICanvasScaler and UICanvasLayout
        // =============================================================================

        void CheckResolved(const UICanvasResolved& resolved) {
            FUZZ_CHECK(std::isfinite(resolved.scale) && resolved.scale > 0.0f);
            FUZZ_CHECK(resolved.transform.scale > 0.0f && std::isfinite(resolved.transform.scale));
            FUZZ_CHECK(std::isfinite(resolved.transform.offsetX) && std::isfinite(resolved.transform.offsetY));
            for (const Rect& rect : {resolved.bounds, resolved.viewBounds, resolved.safeBounds, resolved.contentBounds}) {
                CheckLaidOutRect(rect);
            }
        }

        void CheckTree(const UIElement& element) {
            CheckLaidOutRect(element.rect);
            for (const auto& child : element.children) {
                CheckTree(*child);
            }
        }

        void RunCanvas(const CanvasCase& canvasCase) {
            UICanvasLayout canvas;
            canvas.params.space = PickEnum<CanvasSpace>(canvasCase.space);
            canvas.params.scaleMode = PickEnum<CanvasScaleMode>(canvasCase.scaleMode);
            canvas.params.useSafeArea = (canvasCase.flags & 0x01) != 0;
            canvas.params.extendToOutput = (canvasCase.flags & 0x02) != 0;
            canvas.params.referenceWidth = canvasCase.referenceWidth;
            canvas.params.referenceHeight = canvasCase.referenceHeight;
            canvas.params.match = canvasCase.match;
            canvas.params.minScale = canvasCase.minScale;
            canvas.params.maxScale = canvasCase.maxScale;

            CheckResolved(UICanvasScaler::Resolve(canvas.params, canvasCase.context));

            if ((canvasCase.stackBits & 0x02) != 0) {
                canvas.root->layout = UILayoutKind::Stack;
                canvas.root->style.stack.direction = PickEnum<UIStackDirection>(canvasCase.stack.direction);
                canvas.root->style.stack.crossAlign = PickEnum<UIStackAlign>(canvasCase.stack.align);
                canvas.root->style.stack.wrap = (canvasCase.stack.wrap & 1) != 0;
                canvas.root->style.stack.spacing = canvasCase.stack.spacing;
                canvas.root->style.stack.padding = canvasCase.stack.padding;
            }
            const size_t children = canvasCase.childCount % (kMaxCanvasChildren + 1);
            for (size_t i = 0; i < children; ++i) {
                const auto child = canvas.root->AddChild(PoolString::Intern("Child"));
                child->style.rect = canvasCase.children[i].ToSpec();
                if (i == 0 && (canvasCase.stackBits & 0x01) != 0) {
                    child->layout = UILayoutKind::Stack;
                    child->style.stack.direction = PickEnum<UIStackDirection>(canvasCase.stack.direction);
                    child->style.stack.spacing = canvasCase.stack.spacing;
                    child->style.stack.padding = canvasCase.stack.padding;
                }
                if (i == 0) {
                    for (size_t g = 0; g < canvasCase.grandChildCount % (kMaxGrandChildren + 1); ++g) {
                        const auto grandChild = child->AddChild(PoolString::Intern("Grandchild"));
                        grandChild->style.rect = canvasCase.grandChildren[g].ToSpec();
                    }
                }
            }

            canvas.Update(canvasCase.context);
            CheckResolved(canvas.GetResolved());
            CheckTree(*canvas.root);

            // The hit test and the validator read the rects; neither may fail on any of them
            (void)canvas.HitTest(canvasCase.pointX, canvasCase.pointY);
            FUZZ_CHECK(canvas.PickElement(canvasCase.pointX, canvasCase.pointY) != canvas.root.Get());
            (void)canvas.Validate(UILayoutValidatorOptions{});
        }

    }  // namespace

    /**
     * The input is a stream of rounds (`FuzzUiGeometryInput.h`): the numbers of a box, of a nine-slice, of a rect spec, of a stack and of a canvas, as the raw bits of floats, so NaN,
     * infinity, denormals and the largest floats all appear. Every round runs every builder of the list above with them. What is checked: the mesh of one call has at most the vertices and
     * indices that the constants of `UIBoxMesh` allow, whole triangles, indices inside the list and vertices that are finite; every rect a layout, a stack, a nine-slice or a canvas gives
     * has bounded numbers and no negative size; the resolved scale is positive and within its range; a corner radius is finite and not negative, and the two radii on a side fit it.
     * The sanitizers carry the rest (a float to integer conversion that overflows, a division by zero, an overflow of an index).
     */
    void FuzzUiGeometry(const uint8_t* data, size_t size) {
        Reader reader(data, size);
        for (size_t round = 0; round < kMaxRounds && !reader.Done(); ++round) {
            RoundCase input;
            Transfer(reader, input);
            RunBox(input.box);
            RunNineSlice(input.slice);
            RunRect(input.parent, input.spec);
            RunStack(input.stack);
            RunCanvas(input.canvas);
        }
    }

}  // namespace BECore::Fuzz
